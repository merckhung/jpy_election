#include "src/election/results_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace jpy::election {
namespace {

// SplitMix64: tiny, deterministic hash for reproducible simulations.
uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

uint64_t HashString(std::string_view s, uint64_t seed) {
  uint64_t h = seed ^ 0xCBF29CE484222325ull;
  for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
  return Mix(h);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

// Kumaraswamy(a, b) quantile: skewed timing on [0, 1].
double Kumaraswamy(double u, double a, double b) {
  return std::pow(1.0 - std::pow(1.0 - u, 1.0 / b), 1.0 / a);
}

// Splits `total` over batches so that batch s gets round(total * cum[s]) -
// round(total * cum[s-1]); the parts always sum to `total` exactly.
void SplitExact(int64_t total, const std::vector<double>& cum, std::vector<int64_t>* out) {
  out->assign(cum.size(), 0);
  int64_t prev = 0;
  for (size_t s = 0; s < cum.size(); ++s) {
    const int64_t upto =
        s + 1 == cum.size() ? total : static_cast<int64_t>(std::llround(total * cum[s]));
    (*out)[s] = std::max<int64_t>(0, upto - prev);
    prev = std::max(prev, upto);
  }
}

std::vector<double> Cumulative(const std::vector<double>& w) {
  std::vector<double> cum(w.size());
  double sum = 0;
  for (double x : w) sum += x;
  double acc = 0;
  for (size_t i = 0; i < w.size(); ++i) {
    acc += w[i];
    cum[i] = sum > 0 ? acc / sum : static_cast<double>(i + 1) / w.size();
  }
  return cum;
}

std::string NextDay(const std::string& date) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return date;
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  const int days = m == 2 && leap ? 29 : kDays[std::clamp(m, 1, 12) - 1];
  if (++d > days) {
    d = 1;
    if (++m > 12) {
      m = 1;
      ++y;
    }
  }
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
  return buf;
}

}  // namespace

FileResultsSource::FileResultsSource(const ElectionData* data, std::string path,
                                     double poll_interval_s)
    : data_(data), path_(std::move(path)), poll_interval_s_(poll_interval_s) {}

std::shared_ptr<const ResultsSnapshot> FileResultsSource::Poll(double now) {
  if (now - last_poll_ < poll_interval_s_) return nullptr;
  last_poll_ = now;
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path_, ec);
  if (ec) {
    last_error_ = "cannot stat " + path_;
    return nullptr;
  }
  if (loaded_once_ && mtime == last_mtime_) return nullptr;
  std::string text;
  if (!ReadFile(path_, &text)) {
    last_error_ = "cannot read " + path_;
    return nullptr;
  }
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  if (!ParseResultsJson(text, *data_, snap.get(), &error)) {
    // Keep the previous snapshot; a writer may be mid-way through the file.
    last_error_ = error;
    return nullptr;
  }
  last_error_.clear();
  last_mtime_ = mtime;
  loaded_once_ = true;
  snap->version = ++version_;
  return snap;
}

std::string FileResultsSource::Describe() const { return "file: " + path_; }

SimulatedResultsSource::SimulatedResultsSource(const ElectionData* data,
                                               const geo::RegionTree* tree, uint64_t seed,
                                               double speed,
                                               std::shared_ptr<const ResultsSnapshot> reference,
                                               Mode mode)
    : data_(data), tree_(tree), seed_(seed), reference_(std::move(reference)), mode_(mode) {
  if (!reference_) mode_ = Mode::kSynthetic;  // nothing to replay
  set_speed(speed);
  Build();
}

void SimulatedResultsSource::Build() {
  const auto& races = data_->races();
  const auto& refs = data_->info().referendums;
  const size_t nrefs = refs.size();
  const bool replay = mode_ == Mode::kReplay;
  auto index_of = [&](const Race* r) { return r ? static_cast<int>(r - races.data()) : -1; };
  auto ref_tally = [&](const Race& race, const std::string& code) -> const Tally* {
    if (!reference_) return nullptr;
    auto it = reference_->races.find(race.id);
    if (it == reference_->races.end()) return nullptr;
    auto t = it->second.find(code);
    return t == it->second.end() ? nullptr : &t->second;
  };

  // Counting units: every unit of a district with an SMD race (and, with a
  // reference, only units that actually voted - not the Northern Territories).
  for (int id = 0; id < tree_->size(); ++id) {
    const geo::Region& r = tree_->region(id);
    if (r.level != geo::Level::kVillage) continue;
    const Race* smd = data_->SmdRaceForDistrict(r.district_code);
    if (!smd) continue;
    if (reference_ && !ref_tally(*smd, r.code)) continue;
    CountingUnit u;
    u.region = id;
    u.smd = index_of(smd);
    u.pr = index_of(data_->PrRaceForPref(r.county_code));
    u.pref = tree_->AncestorAt(id, geo::Level::kCounty);
    units_.push_back(u);
  }

  // Synthetic, party-blind strengths per race.
  struct RaceGen {
    std::vector<double> strength;
    int front_a = -1, front_b = -1;
    double corr = 0;
  };
  std::vector<RaceGen> gen(races.size());
  for (size_t ri = 0; ri < races.size(); ++ri) {
    const Race& race = races[ri];
    const size_t n = race.candidates.size();
    if (n == 0) continue;
    RaceGen& g = gen[ri];
    const uint64_t rh = HashString(race.id, seed_);
    g.strength.resize(n);
    for (size_t i = 0; i < n; ++i) {
      const double u = Unit(HashString(race.candidates[i].id, seed_));
      g.strength[i] = race.is_pr() ? 0.01 + std::pow(u, 2.0) : 0.04 + std::pow(u, 2.4);
    }
    if (race.is_pr()) continue;
    std::vector<int> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return g.strength[a] > g.strength[b]; });
    g.front_a = order[0];
    g.front_b = n > 1 ? order[1] : order[0];
    // About a quarter of districts are neck and neck, a third are safe and
    // the rest are competitive.
    const double tight = Unit(Mix(rh + 9));
    const double ratio = tight < 0.25   ? 0.985 + 0.02 * Unit(Mix(rh + 1))
                         : tight < 0.62 ? 0.78 + 0.18 * Unit(Mix(rh + 1))
                                        : 0.35 + 0.3 * Unit(Mix(rh + 1));
    g.strength[g.front_b] = g.strength[g.front_a] * ratio;
    // Correlation between reporting time and lean: > 0 means early units
    // favour B and late ones favour A (so A comes from behind).
    const double u = Unit(Mix(rh + 2));
    g.corr = u < 0.35 ? 0.0 : (u < 0.7 ? 1.0 : -1.0) * (0.35 + 0.5 * Unit(Mix(rh + 3)));
  }
  std::vector<double> review_base(nrefs);
  for (size_t j = 0; j < nrefs; ++j) {
    review_base[j] = 0.07 + 0.06 * Unit(HashString(refs[j].id, seed_ ^ 0x5EED));
  }

  struct ReviewSlot {
    size_t report;
    double weight;
  };
  std::vector<std::vector<ReviewSlot>> review_slots(tree_->size());
  std::vector<int64_t> pref_eligible(tree_->size(), 0), pref_cast(tree_->size(), 0);

  std::vector<int64_t> parts;
  for (CountingUnit& u : units_) {
    const geo::Region& region = tree_->region(u.region);
    const uint64_t uh = HashString(region.code, seed_);
    const int n = std::clamp(3 + static_cast<int>(Unit(Mix(uh + 3)) * 6), 3, 8);
    u.batches = n;
    std::vector<double> w(n);
    for (int s = 0; s < n; ++s) w[s] = 0.4 + Unit(Mix(uh + 11 * (s + 1)));
    const std::vector<double> cum = Cumulative(w);

    // Timing (minutes after 20:00).
    const double k = Kumaraswamy(Unit(Mix(uh + 1)), 1.8, 2.6);
    std::vector<float> smd_t(n), pr_t(n), rev_t(n);
    double t = 55 + 250 * k + (Unit(Mix(uh + 4)) - 0.5) * 30;
    for (int s = 0; s < n; ++s) {
      smd_t[s] = static_cast<float>(std::clamp(t, 45.0, 465.0));
      t += 6 + 26 * Unit(Mix(uh + 50 + s));
    }
    t = smd_t[0] + 25 + 50 * Unit(Mix(uh + 5));
    for (int s = 0; s < n; ++s) {
      pr_t[s] = static_cast<float>(std::clamp(t, 60.0, 472.0));
      t += 6 + 24 * Unit(Mix(uh + 70 + s));
    }
    t = std::max(smd_t[n - 1], pr_t[n - 1]) + 15 + 60 * Unit(Mix(uh + 6));
    for (int s = 0; s < n; ++s) {
      rev_t[s] = static_cast<float>(std::clamp(t, 120.0, 478.0));
      t += 4 + 14 * Unit(Mix(uh + 90 + s));
    }

    // Unit totals.
    const Race& smd = races[u.smd];
    const Tally* rt = ref_tally(smd, region.code);
    int64_t eligible = rt && rt->eligible > 0
                           ? rt->eligible
                           : 8000 + static_cast<int64_t>(Unit(Mix(uh + 7)) * 150000);
    int64_t cast = rt && rt->ballots_cast > 0
                       ? rt->ballots_cast
                       : static_cast<int64_t>(eligible * (0.48 + 0.14 * Unit(Mix(uh + 8))));
    const size_t nc = smd.candidates.size();
    std::vector<int64_t> smd_votes(nc, 0);
    if (replay && rt) {
      for (size_t i = 0; i < nc && i < rt->votes.size(); ++i) smd_votes[i] = rt->votes[i];
    } else {
      const RaceGen& g = gen[u.smd];
      const double lean = g.corr * (1.0 - 2.0 * k) + 0.35 * (Unit(Mix(uh + 2)) * 2 - 1);
      const int64_t valid = static_cast<int64_t>(cast * 0.975);
      std::vector<double> sw(nc);
      double sum = 0;
      for (size_t i = 0; i < nc; ++i) {
        sw[i] = g.strength[i] * (0.75 + 0.5 * Unit(Mix(uh + 17 * (i + 7))));
        if (static_cast<int>(i) == g.front_a) sw[i] *= 1.0 - 0.3 * lean;
        if (static_cast<int>(i) == g.front_b && g.front_b != g.front_a) sw[i] *= 1.0 + 0.3 * lean;
        sum += sw[i];
      }
      for (size_t i = 0; i < nc; ++i) {
        smd_votes[i] = static_cast<int64_t>(std::llround(valid * sw[i] / sum));
      }
    }
    // Party lists.
    std::vector<int64_t> pr_votes;
    int64_t pr_eligible = eligible, pr_cast = cast;
    if (u.pr >= 0) {
      const Race& pr = races[u.pr];
      const size_t np = pr.candidates.size();
      pr_votes.assign(np, 0);
      const Tally* pt = ref_tally(pr, region.code);
      if (pt && pt->eligible > 0) pr_eligible = pt->eligible;
      if (pt && pt->ballots_cast > 0) pr_cast = pt->ballots_cast;
      if (replay && pt) {
        for (size_t i = 0; i < np && i < pt->votes.size(); ++i) pr_votes[i] = pt->votes[i];
      } else {
        const RaceGen& g = gen[u.pr];
        const int64_t valid = static_cast<int64_t>(pr_cast * 0.97);
        std::vector<double> pw(np);
        double sum = 0;
        for (size_t i = 0; i < np; ++i) {
          pw[i] = g.strength[i] * (0.8 + 0.4 * Unit(Mix(uh + 31 * (i + 3))));
          sum += pw[i];
        }
        for (size_t i = 0; i < np; ++i) {
          pr_votes[i] = sum > 0 ? static_cast<int64_t>(std::llround(valid * pw[i] / sum)) : 0;
        }
      }
    }

    // District batches: each candidate's share varies a little from batch
    // to batch, but every candidate's batches sum to the unit total.
    auto emit_batches = [&](Kind kind, int race, const std::vector<int64_t>& votes,
                            int64_t elig, int64_t cst, const std::vector<float>& times,
                            uint64_t salt) {
      std::vector<std::vector<int64_t>> per_cand(votes.size());
      for (size_t i = 0; i < votes.size(); ++i) {
        std::vector<double> wi(n);
        for (int s = 0; s < n; ++s) wi[s] = w[s] * (0.75 + 0.5 * Unit(Mix(uh + salt + 97 * i + s)));
        SplitExact(votes[i], Cumulative(wi), &per_cand[i]);
      }
      std::vector<int64_t> elig_parts, cast_parts;
      SplitExact(elig, cum, &elig_parts);
      SplitExact(cst, cum, &cast_parts);
      for (int s = 0; s < n; ++s) {
        Report rep;
        rep.minute = times[s];
        rep.kind = kind;
        rep.race = race;
        rep.region = u.region;
        rep.eligible = static_cast<int32_t>(elig_parts[s]);
        rep.cast = static_cast<int32_t>(cast_parts[s]);
        rep.votes_at = static_cast<uint32_t>(votes_.size());
        for (size_t i = 0; i < votes.size(); ++i) {
          votes_.push_back(static_cast<int32_t>(per_cand[i][s]));
        }
        reports_.push_back(rep);
      }
    };
    emit_batches(Kind::kSmd, u.smd, smd_votes, eligible, cast, smd_t, 1000);
    if (u.pr >= 0) emit_batches(Kind::kPr, u.pr, pr_votes, pr_eligible, pr_cast, pr_t, 2000);

    // Review batches: filled below from prefecture totals.
    if (nrefs > 0 && u.pref >= 0) {
      pref_eligible[u.pref] += eligible;
      pref_cast[u.pref] += cast;
      for (int s = 0; s < n; ++s) {
        Report rep;
        rep.minute = rev_t[s];
        rep.kind = Kind::kReview;
        rep.race = -1;
        rep.region = u.pref;
        rep.eligible = 0;
        rep.cast = 0;
        rep.votes_at = static_cast<uint32_t>(votes_.size());
        votes_.resize(votes_.size() + 2 * nrefs, 0);
        review_slots[u.pref].push_back({reports_.size(), static_cast<double>(cast) * w[s]});
        reports_.push_back(rep);
      }
    }
  }

  // National review per prefecture: the prefecture totals are split over
  // its batches in reporting order, so they sum up exactly.
  review_batches_.assign(tree_->size(), 0);
  for (int pref = 0; pref < tree_->size(); ++pref) {
    auto& slots = review_slots[pref];
    if (slots.empty()) continue;
    review_batches_[pref] = static_cast<int>(slots.size());
    std::stable_sort(slots.begin(), slots.end(), [&](const ReviewSlot& a, const ReviewSlot& b) {
      return reports_[a.report].minute < reports_[b.report].minute;
    });
    std::vector<double> sw;
    for (const ReviewSlot& s : slots) sw.push_back(s.weight);
    const std::vector<double> cum = Cumulative(sw);
    const std::string& code = tree_->region(pref).code;
    int64_t eligible = pref_eligible[pref], cast = pref_cast[pref];
    std::vector<int64_t> agree(nrefs, 0), disagree(nrefs, 0);
    const uint64_t ph = HashString(code, seed_ ^ 0xAE71);
    for (size_t j = 0; j < nrefs; ++j) {
      const RefTally* rt = nullptr;
      if (reference_) {
        auto it = reference_->referendums.find(refs[j].id);
        if (it != reference_->referendums.end()) {
          auto t = it->second.find(code);
          if (t != it->second.end()) rt = &t->second;
        }
      }
      if (rt && j == 0 && rt->eligible > 0) {
        eligible = rt->eligible;
        cast = rt->ballots_cast;
      }
      if (replay && rt) {
        agree[j] = rt->agree;
        disagree[j] = rt->disagree;
      } else {
        const int64_t valid = static_cast<int64_t>(cast * 0.93);
        const double share =
            std::clamp(review_base[j] + 0.04 * (Unit(Mix(ph + 13 * j)) - 0.5), 0.02, 0.6);
        agree[j] = static_cast<int64_t>(valid * share);
        disagree[j] = valid - agree[j];
      }
    }
    std::vector<int64_t> elig_parts, cast_parts;
    SplitExact(eligible, cum, &elig_parts);
    SplitExact(cast, cum, &cast_parts);
    for (size_t k = 0; k < slots.size(); ++k) {
      reports_[slots[k].report].eligible = static_cast<int32_t>(elig_parts[k]);
      reports_[slots[k].report].cast = static_cast<int32_t>(cast_parts[k]);
    }
    for (size_t j = 0; j < nrefs; ++j) {
      SplitExact(agree[j], cum, &parts);
      for (size_t k = 0; k < slots.size(); ++k) {
        votes_[reports_[slots[k].report].votes_at + 2 * j] = static_cast<int32_t>(parts[k]);
      }
      SplitExact(disagree[j], cum, &parts);
      for (size_t k = 0; k < slots.size(); ++k) {
        votes_[reports_[slots[k].report].votes_at + 2 * j + 1] = static_cast<int32_t>(parts[k]);
      }
    }
  }

  std::stable_sort(reports_.begin(), reports_.end(),
                   [](const Report& a, const Report& b) { return a.minute < b.minute; });
  last_report_ = reports_.empty() ? 0 : reports_.back().minute;
  last_smd_ = 0;
  for (const Report& r : reports_) {
    if (r.kind == Kind::kSmd) last_smd_ = std::max<double>(last_smd_, r.minute);
  }
  PlanDeclarations();
}

// Plans the media calls and campaign statements. Districts whose final
// margin is at least 20% of the valid votes are called at 20:00 sharp from
// exit polls (ゼロ打ち); the others once the running lead of the eventual
// winner exceeds the votes still out (a few minutes later on air), at the
// latest right after the last batch. Winners speak 10-40 minutes after the
// call; most runners-up concede.
void SimulatedResultsSource::PlanDeclarations() {
  const auto& races = data_->races();
  std::vector<std::vector<int64_t>> final_votes(races.size()), votes(races.size());
  std::vector<int> batches(races.size(), 0), counted(races.size(), 0);
  std::vector<float> last_minute(races.size(), 0);
  for (size_t r = 0; r < races.size(); ++r) {
    final_votes[r].assign(races[r].candidates.size(), 0);
    votes[r].assign(races[r].candidates.size(), 0);
  }
  for (const Report& rep : reports_) {
    if (rep.kind != Kind::kSmd) continue;
    for (size_t i = 0; i < final_votes[rep.race].size(); ++i) {
      final_votes[rep.race][i] += votes_[rep.votes_at + i];
    }
    ++batches[rep.race];
    last_minute[rep.race] = std::max(last_minute[rep.race], rep.minute);
  }
  auto top2 = [](const std::vector<int64_t>& v, int* a, int* b) {
    *a = *b = -1;
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
      if (*a < 0 || v[i] > v[*a]) {
        *b = *a;
        *a = i;
      } else if (*b < 0 || v[i] > v[*b]) {
        *b = i;
      }
    }
  };
  std::vector<float> called_at(races.size(), -1);
  std::vector<int> winner(races.size(), -1), runner(races.size(), -1);
  for (size_t r = 0; r < races.size(); ++r) {
    if (!races[r].is_smd() || batches[r] == 0) continue;
    int a, b;
    top2(final_votes[r], &a, &b);
    winner[r] = a;
    runner[r] = b;
    int64_t total = 0;
    for (int64_t v : final_votes[r]) total += v;
    const int64_t margin = b >= 0 ? final_votes[r][a] - final_votes[r][b] : total;
    // ゼロ打ち: on air the second polls close (30 s in, so that a snapshot
    // taken at 20:00 sharp still shows the pre-call state).
    if (total > 0 && margin * 5 >= total) called_at[r] = 0.5f;
  }
  for (const Report& rep : reports_) {
    if (rep.kind != Kind::kSmd) continue;
    const int r = rep.race;
    int64_t total = 0;
    for (size_t i = 0; i < votes[r].size(); ++i) {
      votes[r][i] += votes_[rep.votes_at + i];
      total += votes[r][i];
    }
    ++counted[r];
    if (called_at[r] >= 0 || winner[r] < 0) continue;
    int a, b;
    top2(votes[r], &a, &b);
    if (a != winner[r] || b < 0) continue;
    const double progress = static_cast<double>(counted[r]) / batches[r];
    const double remaining = static_cast<double>(total) / counted[r] * (batches[r] - counted[r]);
    if (progress >= 0.2 && static_cast<double>(votes[r][a] - votes[r][b]) > remaining) {
      const uint64_t h = HashString(races[r].id, seed_ ^ 0xCA11);
      called_at[r] = std::min<float>(static_cast<float>(kCountMinutes),
                                     rep.minute + 2 + 10 * static_cast<float>(Unit(h)));
    }
  }
  for (size_t r = 0; r < races.size(); ++r) {
    if (winner[r] < 0) continue;
    const uint64_t h = HashString(races[r].id, seed_ ^ 0xDEC1);
    if (called_at[r] < 0) {
      called_at[r] = std::min<float>(static_cast<float>(kCountMinutes),
                                     last_minute[r] + 1 + 4 * static_cast<float>(Unit(Mix(h + 2))));
    }
    const int r_int = static_cast<int>(r);
    declarations_.push_back({called_at[r], r_int, winner[r], Declaration::Type::kCalled});
    const float speech = std::min<float>(static_cast<float>(kCountMinutes),
                                         called_at[r] + 10 + 30 * static_cast<float>(Unit(h)));
    declarations_.push_back({speech, r_int, winner[r], Declaration::Type::kVictory});
    if (runner[r] >= 0 && Unit(Mix(h + 1)) < 0.7) {
      const float concede = std::min<float>(
          static_cast<float>(kCountMinutes), called_at[r] + 5 + 30 * static_cast<float>(Unit(Mix(h + 3))));
      declarations_.push_back({concede, r_int, runner[r], Declaration::Type::kConcede});
    }
  }
  std::stable_sort(declarations_.begin(), declarations_.end(),
                   [](const PlannedDeclaration& x, const PlannedDeclaration& y) {
                     return x.minute < y.minute;
                   });
}

void SimulatedResultsSource::SeekClock(double minutes) {
  clock_ = std::clamp(minutes, 0.0, kCountMinutes);
  emitted_reported_ = SIZE_MAX;  // force a new snapshot
  emitted_declared_ = SIZE_MAX;
}

void SimulatedResultsSource::set_speed(double s) {
  s = std::clamp(s, kMinSpeed, kMaxSpeed);
  speed_ = std::exp2(std::round(std::log2(s)));
}

std::string SimulatedResultsSource::ClockLabel(double minutes) {
  const int m = static_cast<int>(std::floor(std::clamp(minutes, 0.0, 600.0)));
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d", (20 + m / 60) % 24, m % 60);
  return buf;
}

std::string SimulatedResultsSource::Timestamp(const std::string& date, double minutes) {
  const int m = static_cast<int>(std::floor(std::clamp(minutes, 0.0, 600.0)));
  int h = 20 + m / 60;
  std::string day = date.empty() ? "2026-02-08" : date;
  if (h >= 24) {
    h -= 24;
    day = NextDay(day);
  }
  char buf[48];
  std::snprintf(buf, sizeof buf, "%sT%02d:%02d:00+09:00", day.c_str(), h, m % 60);
  return buf;
}

std::shared_ptr<const ResultsSnapshot> SimulatedResultsSource::Poll(double now) {
  if (last_now_ >= 0 && !paused_ && clock_ < kCountMinutes) {
    clock_ = std::min(kCountMinutes, clock_ + (now - last_now_) * speed_ / 60.0);
  }
  last_now_ = now;
  // Like a real feed: publish at most ~5 times per second, and only when new
  // batches have reported.
  if (now - last_emit_ < 0.2 && emitted_reported_ != SIZE_MAX) return nullptr;
  const size_t reported = static_cast<size_t>(
      std::upper_bound(reports_.begin(), reports_.end(), clock_,
                       [](double t, const Report& s) { return t < s.minute; }) -
      reports_.begin());
  const size_t declared = static_cast<size_t>(
      std::upper_bound(declarations_.begin(), declarations_.end(), clock_,
                       [](double t, const PlannedDeclaration& d) { return t < d.minute; }) -
      declarations_.begin());
  if (reported == emitted_reported_ && declared == emitted_declared_) return nullptr;
  emitted_declared_ = declared;
  last_emit_ = now;
  emitted_reported_ = reported;
  auto snap = SnapshotAtClock(clock_);
  snap->version = ++version_;
  return snap;
}

std::string SimulatedResultsSource::Describe() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, "%s %s ×%g%s", replay() ? "REPLAY" : "SIMULATION",
                ClockLabel(clock_).c_str(), speed_, paused_ ? " (paused)" : "");
  return buf;
}

std::vector<int64_t> SimulatedResultsSource::InflowHistory(double bucket_minutes,
                                                           double until) const {
  std::vector<int64_t> out;
  const auto& races = data_->races();
  for (const Report& rep : reports_) {
    if (rep.minute > until) break;
    if (rep.kind != Kind::kSmd) continue;
    const size_t b = static_cast<size_t>(rep.minute / bucket_minutes);
    if (out.size() <= b) out.resize(b + 1, 0);
    for (size_t i = 0; i < races[rep.race].candidates.size(); ++i) out[b] += votes_[rep.votes_at + i];
  }
  return out;
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAt(double p) const {
  return SnapshotAtClock(p * kCountMinutes);
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAtClock(double minutes) const {
  minutes = std::clamp(minutes, 0.0, kCountMinutes);
  auto snap = std::make_shared<ResultsSnapshot>();
  snap->simulated = true;
  snap->replay = replay();
  snap->source = replay() ? "REPLAY - official final results revealed on a simulated clock"
                          : "SIMULATION - synthetic numbers, not real results";
  const std::string& date = data_->info().date;
  snap->updated_at = Timestamp(date, minutes);

  const auto& races = data_->races();
  const auto& refs = data_->info().referendums;
  // Every unit appears (so progress denominators are right) even before any
  // of its batches report.
  std::vector<Tally*> smd_tally(tree_->size(), nullptr), pr_tally(tree_->size(), nullptr);
  for (const CountingUnit& u : units_) {
    const std::string& code = tree_->region(u.region).code;
    Tally& t = snap->races[races[u.smd].id][code];
    t.votes.assign(races[u.smd].candidates.size(), 0);
    t.units_total = u.batches;
    t.has_data = true;
    smd_tally[u.region] = &t;
    if (u.pr >= 0) {
      Tally& p = snap->races[races[u.pr].id][code];
      p.votes.assign(races[u.pr].candidates.size(), 0);
      p.units_total = u.batches;
      p.has_data = true;
      pr_tally[u.region] = &p;
    }
  }
  std::vector<std::vector<RefTally*>> review(refs.size(),
                                             std::vector<RefTally*>(tree_->size(), nullptr));
  for (int pref = 0; pref < static_cast<int>(review_batches_.size()); ++pref) {
    if (review_batches_[pref] == 0) continue;
    for (size_t j = 0; j < refs.size(); ++j) {
      RefTally& r = snap->referendums[refs[j].id][tree_->region(pref).code];
      r.units_total = review_batches_[pref];
      r.has_data = true;
      review[j][pref] = &r;
    }
  }
  size_t reported = 0;
  for (const Report& rep : reports_) {
    if (rep.minute > minutes) break;
    ++reported;
    if (rep.kind == Kind::kReview) {
      for (size_t j = 0; j < refs.size(); ++j) {
        RefTally* r = review[j][rep.region];
        if (!r) continue;
        r->agree += votes_[rep.votes_at + 2 * j];
        r->disagree += votes_[rep.votes_at + 2 * j + 1];
        r->eligible += rep.eligible;
        r->ballots_cast += rep.cast;
        r->units_counted += 1;
      }
      continue;
    }
    Tally* t = rep.kind == Kind::kSmd ? smd_tally[rep.region] : pr_tally[rep.region];
    if (!t) continue;
    for (size_t i = 0; i < t->votes.size(); ++i) t->votes[i] += votes_[rep.votes_at + i];
    t->eligible += rep.eligible;
    t->ballots_cast += rep.cast;
    t->units_counted += 1;
  }
  for (const PlannedDeclaration& d : declarations_) {
    if (d.minute > minutes) break;
    const Race& race = races[d.race];
    snap->declarations.push_back(
        {race.id, race.candidates[d.candidate].id, d.type, Timestamp(date, d.minute)});
  }
  snap->status = reported == 0                 ? ResultsStatus::kPreElection
                 : reported == reports_.size() ? ResultsStatus::kFinal
                                               : ResultsStatus::kCounting;
  return snap;
}

}  // namespace jpy::election
