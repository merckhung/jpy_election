#include "src/election/seats.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace jpy::election {

const SmdOutcome* SeatSummary::Smd(const Race* race) const {
  for (const SmdOutcome& o : smd) {
    if (o.race == race) return &o;
  }
  return nullptr;
}

const PrOutcome* SeatSummary::Pr(const Race* race) const {
  for (const PrOutcome& o : pr) {
    if (o.race == race) return &o;
  }
  return nullptr;
}

const PartySeats* SeatSummary::Party(std::string_view code) const {
  for (const PartySeats& p : parties) {
    if (p.party == code) return &p;
  }
  return nullptr;
}

std::vector<int> DHondt(const std::vector<int64_t>& votes, const std::vector<int>& caps,
                        int seats) {
  const int n = static_cast<int>(votes.size());
  std::vector<int> out(n, 0);
  struct Quotient {
    int64_t votes;
    int divisor;
    int list;
  };
  std::vector<Quotient> q;
  q.reserve(static_cast<size_t>(n) * std::max(0, seats));
  for (int i = 0; i < n; ++i) {
    if (votes[i] <= 0) continue;
    for (int d = 1; d <= seats; ++d) q.push_back({votes[i], d, i});
  }
  // votes_a / d_a > votes_b / d_b  <=>  votes_a * d_b > votes_b * d_a.
  std::sort(q.begin(), q.end(), [](const Quotient& a, const Quotient& b) {
    const int64_t l = a.votes * b.divisor, r = b.votes * a.divisor;
    if (l != r) return l > r;
    if (a.list != b.list) return a.list < b.list;
    return a.divisor < b.divisor;
  });
  int left = seats;
  for (const Quotient& x : q) {
    if (left <= 0) break;
    if (!caps.empty() && out[x.list] >= caps[x.list]) continue;
    ++out[x.list];
    --left;
  }
  return out;
}

double Sekihairitsu(const Tally& t, int cand) {
  const int w = t.Leader();
  if (w < 0 || cand < 0 || cand >= static_cast<int>(t.votes.size()) || t.votes[w] <= 0) return 0;
  return 100.0 * static_cast<double>(t.votes[cand]) / static_cast<double>(t.votes[w]);
}

SeatSummary ComputeSeats(const ElectionData& data, ResultsView& view, const CalledFn& called) {
  SeatSummary s;
  s.total_seats = data.info().total_seats;
  s.majority = data.info().majority;
  s.supermajority = data.info().supermajority;

  // SMD candidate id -> (SMD outcome index, candidate index).
  std::unordered_map<std::string, std::pair<int, int>> smd_of;
  std::vector<const Tally*> smd_tally;
  for (const Race& race : data.races()) {
    if (!race.is_smd()) continue;
    const Tally& t = view.RaceTotal(race);
    SmdOutcome o;
    o.race = &race;
    o.valid = t.TotalVotes();
    o.progress = t.Progress();
    o.final = t.units_total > 0 && t.units_counted >= t.units_total;
    const std::vector<int> rank = t.Ranking();
    if (o.valid > 0) {
      o.leader = rank[0];
      o.runner_up = rank.size() > 1 ? rank[1] : -1;
    }
    const int c = called ? called(race) : -1;
    if (c >= 0 && c < static_cast<int>(race.candidates.size())) {
      o.called = true;
      if (o.leader != c) {
        // A call from exit polls / projections can precede the count's lead.
        o.runner_up = o.leader;
        o.leader = c;
      }
    }
    if (o.leader >= 0 && o.runner_up >= 0) o.margin = t.votes[o.leader] - t.votes[o.runner_up];
    else if (o.leader >= 0) o.margin = t.votes[o.leader];
    o.decided = o.leader >= 0 && (o.final || o.called);
    const int idx = static_cast<int>(s.smd.size());
    for (size_t i = 0; i < race.candidates.size(); ++i) {
      smd_of[race.candidates[i].id] = {idx, static_cast<int>(i)};
    }
    s.smd.push_back(o);
    smd_tally.push_back(&t);
    if (o.valid > 0) ++s.smd_reporting;
    if (o.decided) ++s.smd_decided;
  }

  for (const Race& race : data.races()) {
    if (!race.is_pr()) continue;
    const Tally& t = view.RaceTotal(race);
    PrOutcome o;
    o.race = &race;
    o.valid = t.TotalVotes();
    o.progress = t.Progress();
    o.final = t.units_total > 0 && t.units_counted >= t.units_total;
    const size_t n = race.candidates.size();
    std::vector<int64_t> votes(n, 0);
    std::vector<int> caps(n, 0);
    // Eligible entries per list, with their tie-break key.
    struct Eligible {
      int no;
      double sekihairitsu;
      int index;
    };
    std::vector<std::vector<Eligible>> eligible(n);
    for (size_t li = 0; li < n; ++li) {
      votes[li] = li < t.votes.size() ? t.votes[li] : 0;
      const Candidate& list = race.candidates[li];
      for (size_t k = 0; k < list.list.size(); ++k) {
        const ListEntry& e = list.list[k];
        double sek = 0;
        bool ok = true;
        auto it = e.candidate.empty() ? smd_of.end() : smd_of.find(e.candidate);
        if (it != smd_of.end()) {
          const SmdOutcome& so = s.smd[it->second.first];
          const Tally& st = *smd_tally[it->second.first];
          const int c = it->second.second;
          const int64_t cv = c < static_cast<int>(st.votes.size()) ? st.votes[c] : 0;
          if (so.leader == c) ok = false;                          // wins the district
          else if (so.valid > 0 && cv * 10 < so.valid) ok = false;  // < 10%: struck off
          sek = Sekihairitsu(st, c);
        } else {
          ok = !e.disqualified;
        }
        if (ok) eligible[li].push_back({e.no, sek, static_cast<int>(k)});
      }
      caps[li] = static_cast<int>(eligible[li].size());
    }
    std::vector<int> seats(n, 0), uncapped(n, 0);
    if (o.valid > 0) {
      seats = DHondt(votes, caps, race.seats);
      uncapped = DHondt(votes, {}, race.seats);
    }
    for (size_t li = 0; li < n; ++li) {
      PrListOutcome lo;
      lo.list = static_cast<int>(li);
      lo.votes = votes[li];
      lo.seats = seats[li];
      lo.uncapped_seats = uncapped[li];
      lo.cap = caps[li];
      o.forfeited += std::max(0, uncapped[li] - seats[li]);
      std::vector<Eligible>& el = eligible[li];
      std::stable_sort(el.begin(), el.end(), [](const Eligible& a, const Eligible& b) {
        if (a.no != b.no) return a.no < b.no;
        if (a.sekihairitsu != b.sekihairitsu) return a.sekihairitsu > b.sekihairitsu;
        return a.index < b.index;
      });
      for (int k = 0; k < lo.seats && k < static_cast<int>(el.size()); ++k) {
        lo.elected.push_back(el[k].index);
      }
      o.lists.push_back(std::move(lo));
    }
    s.pr_votes += o.valid;
    if (o.final) ++s.pr_final;
    s.pr.push_back(std::move(o));
  }

  // Chamber totals by party.
  std::unordered_map<std::string, PartySeats> by_party;
  for (const SmdOutcome& o : s.smd) {
    if (o.leader < 0) continue;
    PartySeats& p = by_party[o.race->candidates[o.leader].party];
    ++p.smd;
    if (o.decided) ++p.smd_decided;
  }
  for (const PrOutcome& o : s.pr) {
    for (const PrListOutcome& lo : o.lists) {
      const std::string& party = o.race->candidates[lo.list].party;
      if (lo.seats == 0 && lo.votes == 0) continue;
      PartySeats& p = by_party[party];
      p.pr += lo.seats;
      if (o.final) p.pr_decided += lo.seats;
      p.pr_votes += lo.votes;
    }
  }
  for (auto& [code, p] : by_party) {
    p.party = code;
    s.parties.push_back(p);
  }
  std::sort(s.parties.begin(), s.parties.end(), [](const PartySeats& a, const PartySeats& b) {
    if (a.total() != b.total()) return a.total() > b.total();
    if (a.pr_votes != b.pr_votes) return a.pr_votes > b.pr_votes;
    return a.party < b.party;
  });
  return s;
}

}  // namespace jpy::election
