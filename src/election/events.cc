#include "src/election/events.h"

#include <algorithm>
#include <cstdio>

namespace jpy::election {

bool IsBreaking(EventType t) {
  switch (t) {
    case EventType::kLeadChange:
    case EventType::kVictoryDeclared:
    case EventType::kConcession:
    case EventType::kCalled:
    case EventType::kIncumbentTrailing:
      return true;
    default:
      return false;
  }
}

const char* EventKey(EventType t) {
  switch (t) {
    case EventType::kFirstReturns: return "first";
    case EventType::kLeadChange: return "lead_change";
    case EventType::kVictoryDeclared: return "victory";
    case EventType::kConcession: return "concede";
    case EventType::kCalled: return "called";
    case EventType::kIncumbentTrailing: return "incumbent_trailing";
    case EventType::kCloseRace: return "close";
    case EventType::kFinal: return "final";
    case EventType::kPrSeats: return "pr_seats";
  }
  return "";
}

std::string TimeLabel(const std::string& updated_at) {
  const size_t t = updated_at.find('T');
  if (t == std::string::npos || updated_at.size() < t + 6) return "";
  return updated_at.substr(t + 1, 5);
}

double MinutesAfterClose(const std::string& updated_at) {
  int h = 0, m = 0;
  const std::string label = TimeLabel(updated_at);
  if (label.empty() || std::sscanf(label.c_str(), "%d:%d", &h, &m) != 2) return -1;
  if (h < 12) h += 24;  // the count runs past midnight
  return (h - 20) * 60.0 + m;
}

bool EventTracker::Decided(const Tally& t) {
  if (t.units_total <= 0 || t.units_counted <= 0) return false;
  const std::vector<int> rank = t.Ranking();
  if (rank.size() < 2) return t.units_counted >= t.units_total;
  const int64_t margin = t.votes[rank[0]] - t.votes[rank[1]];
  if (margin <= 0) return false;
  if (t.units_counted >= t.units_total) return true;
  // Require a meaningful share of the count before projecting.
  if (t.Progress() < 0.25) return false;
  const double per_unit = static_cast<double>(t.TotalVotes()) / t.units_counted;
  const double remaining = per_unit * (t.units_total - t.units_counted) * 1.15;
  return static_cast<double>(margin) > remaining;
}

bool EventTracker::IsCalled(const std::string& race_id) const {
  auto it = races_.find(race_id);
  return it != races_.end() && it->second.called;
}

int EventTracker::CalledCandidate(const std::string& race_id) const {
  auto it = races_.find(race_id);
  return it != races_.end() && it->second.called ? it->second.called_candidate : -1;
}

void EventTracker::Reset() {
  has_baseline_ = false;
  regions_.clear();
  races_.clear();
}

EventBatch EventTracker::Update(ResultsView& view, const std::vector<Race>& races,
                                const SeatSummary* seats) {
  EventBatch batch;
  batch.baseline = !has_baseline_;
  const std::string time = TimeLabel(view.snapshot().updated_at);

  std::unordered_map<std::string, std::vector<const Declaration*>> decls_by_race;
  for (const Declaration& d : view.snapshot().declarations) decls_by_race[d.race_id].push_back(&d);
  std::unordered_map<int, int64_t> pref_totals;  // prefecture region -> SMD votes

  for (const Race& race : races) {
    const int home = view.RaceRegion(race);
    const Tally& t = view.RaceTotal(race);
    const int64_t total = t.TotalVotes();
    const int leader = total > 0 ? t.Leader() : -1;

    if (race.is_smd()) {
      // District + counting units: deltas and local lead flips.
      std::vector<int> regions = {home};
      for (int u : tree_->region(home).children) regions.push_back(u);
      for (int r : regions) {
        const Tally& rt = view.RaceTally(race, r);
        const int64_t rtotal = rt.TotalVotes();
        const int rleader = rtotal > 0 ? rt.Leader() : -1;
        RegionState& prev = regions_[r];
        if (!batch.baseline) {
          if (rtotal > prev.total) {
            batch.deltas.push_back({r, rtotal - prev.total});
            if (r == home) batch.votes_added += rtotal - prev.total;
          }
          if (prev.leader >= 0 && rleader >= 0 && rleader != prev.leader) {
            batch.flips.push_back({r, &race, rleader, prev.leader});
          }
        }
        prev.total = rtotal;
        prev.leader = rleader;
      }
      const int pref = tree_->AncestorAt(home, geo::Level::kCounty);
      if (pref >= 0) pref_totals[pref] += total;
    }

    RaceState& rs = races_[race.id];
    const int64_t prev_total = batch.baseline ? total : rs.total;
    rs.total = total;
    const std::vector<int> rank = t.Ranking();
    const int64_t margin =
        total > 0 && rank.size() > 1 ? t.votes[rank[0]] - t.votes[rank[1]] : total;
    const double progress = t.Progress();
    auto emit = [&](EventType type, int who, int other) {
      ElectionEvent e;
      e.type = type;
      e.race = &race;
      e.leader = who;
      e.previous = other;
      e.margin = margin;
      e.progress = progress;
      e.time_label = time;
      batch.events.push_back(e);
      return &batch.events.back();
    };
    // Best-placed sitting member (前) in an SMD race.
    int incumbent = -1;
    if (race.is_smd()) {
      for (int c : rank) {
        if (race.candidates[c].incumbent) {
          incumbent = c;
          break;
        }
      }
    }
    const std::vector<const Declaration*>& decls = decls_by_race[race.id];
    std::vector<int> pr_seats;
    if (race.is_pr() && seats) {
      if (const PrOutcome* po = seats->Pr(&race)) {
        for (const PrListOutcome& lo : po->lists) pr_seats.push_back(lo.seats);
      }
    }

    if (batch.baseline) {
      rs.called = race.is_smd() && Decided(t);
      rs.called_candidate = rs.called ? leader : -1;
      rs.final = t.units_total > 0 && t.units_counted >= t.units_total;
      rs.close_alerted = true;
      rs.incumbent_alerted = incumbent >= 0 && leader >= 0 && leader != incumbent;
      rs.declarations_seen = decls.size();
      rs.leader = leader;
      for (const Declaration* d : decls) {
        const int who = race.CandidateIndex(d->candidate_id);
        if (d->type == Declaration::Type::kVictory) rs.declared = who;
        if (d->type == Declaration::Type::kCalled && !rs.called) {
          rs.called = true;
          rs.called_candidate = who;
        }
      }
      rs.pr_seats = pr_seats;
      continue;
    }
    if (prev_total == 0 && total > 0) {
      emit(EventType::kFirstReturns, leader, -1);
      rs.leader = leader;
    }
    // Lead changes need a small margin (0.1% of votes, at least 30) so a
    // neck-and-neck race does not flip-flop with every batch.
    if (race.is_smd() && rs.leader >= 0 && leader >= 0 && leader != rs.leader &&
        margin >= std::max<int64_t>(30, total / 1000)) {
      ElectionEvent* e = emit(EventType::kLeadChange, leader, rs.leader);
      e->after_declaration = rs.declared == rs.leader || rs.called_candidate == rs.leader;
      rs.leader = leader;
    }
    for (size_t k = rs.declarations_seen; k < decls.size(); ++k) {
      const int who = race.CandidateIndex(decls[k]->candidate_id);
      ElectionEvent* e = nullptr;
      switch (decls[k]->type) {
        case Declaration::Type::kVictory:
          rs.declared = who;
          e = emit(EventType::kVictoryDeclared, who, -1);
          break;
        case Declaration::Type::kConcede:
          e = emit(EventType::kConcession, who, rs.called_candidate >= 0 ? rs.called_candidate : leader);
          break;
        case Declaration::Type::kCalled:
          if (rs.called) break;
          rs.called = true;
          rs.called_candidate = who;
          e = emit(EventType::kCalled, who, -1);
          e->zero_call = progress <= 0.001;
          break;
      }
      if (e && !TimeLabel(decls[k]->time).empty()) e->time_label = TimeLabel(decls[k]->time);
    }
    rs.declarations_seen = decls.size();
    if (race.is_smd()) {
      if (!rs.incumbent_alerted && incumbent >= 0 && leader >= 0 && leader != incumbent &&
          progress >= 0.3) {
        rs.incumbent_alerted = true;
        emit(EventType::kIncumbentTrailing, leader, incumbent);
      }
      if (!rs.close_alerted && progress >= 0.7 && total > 0 && margin * 100 < total) {
        rs.close_alerted = true;
        emit(EventType::kCloseRace, leader, rank.size() > 1 ? rank[1] : -1);
      }
      if (!rs.called && Decided(t)) {
        rs.called = true;
        rs.called_candidate = leader;
        emit(EventType::kCalled, leader, -1);
      }
    }
    if (race.is_pr() && !pr_seats.empty()) {
      if (rs.pr_seats.size() == pr_seats.size() && progress >= 0.05) {
        std::vector<int> gain, loss;
        for (size_t i = 0; i < pr_seats.size(); ++i) {
          for (int k = rs.pr_seats[i]; k < pr_seats[i]; ++k) gain.push_back(static_cast<int>(i));
          for (int k = pr_seats[i]; k < rs.pr_seats[i]; ++k) loss.push_back(static_cast<int>(i));
        }
        for (size_t k = 0; k < gain.size(); ++k) {
          ElectionEvent* e = emit(EventType::kPrSeats, gain[k], k < loss.size() ? loss[k] : -1);
          e->margin = pr_seats[gain[k]];
        }
      }
      rs.pr_seats = pr_seats;
    }
    if (!rs.final && t.units_total > 0 && t.units_counted >= t.units_total) {
      rs.final = true;
      emit(EventType::kFinal, leader, -1);
    }
  }
  // Prefecture-level deltas (what the nation view shows).
  for (const auto& [pref, total] : pref_totals) {
    RegionState& prev = regions_[pref];
    if (!batch.baseline && total > prev.total) batch.deltas.push_back({pref, total - prev.total});
    prev.total = total;
  }
  has_baseline_ = true;
  return batch;
}

}  // namespace jpy::election
