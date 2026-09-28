// results_tool: 開票結果ファイル (jpy_election.results/v1) の作成・変換・点検。
// Helpers for producing, converting and inspecting results files for the
// 2026 House of Representatives election (see src/election/results.h).
//
//   bazel run //tools:results_tool -- --template > feed.json
//       全レース・全候補のゼロ票テンプレート (小選挙区は選挙区 "13-01"、比例は
//       都道府県 "13"、国民審査は都道府県単位)。
//       Zeroed entries for every race/candidate: SMD per district, PR lists
//       per prefecture of the bloc, reviews per prefecture.
//   bazel run //tools:results_tool -- --simulate=0.6 [--replay] [--seed=N] > sim.json
//       開票率 60% 時点の模擬スナップショット ("simulated": true)。--replay は
//       実際の最終結果を再生。
//       A unit-level SIMULATED snapshot at 60% of the night (--replay: the
//       real final results revealed over the night).
//   bazel run //tools:results_tool -- --check=FILE [--races]
//       ファイルを検証し、議席 (小選挙区 + 比例) と国民審査を集計。--races で
//       全レースを表示。
//       Validates a file; prints seats by party, PR blocs and reviews
//       (--races: one line per race).
//   bazel run //tools:results_tool -- --rollup=FILE > compact.json
//       市区町村単位のファイルを選挙区 (小選挙区) / 都道府県 (比例・国民審査)
//       単位に集約。
//       Aggregates a unit-level file to districts (SMD) and prefectures (PR,
//       reviews).
//   bazel run //tools:results_tool -- --night [--replay] [--seed=N]
//       模擬開票 (20:00〜04:00) を1分刻みで進め、イベントを表示。
//       Steps a simulated night minute by minute and prints every event.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/election/seats.h"
#include "src/geo/region_tree.h"
#include "tools/cli_utf8.h"

using namespace jpy;

namespace {

constexpr char kUsage[] =
    "使い方: results_tool [--root=DIR] <モード>\n"
    "  --template             ゼロ票テンプレート (小選挙区=選挙区, 比例・国民審査=都道府県)\n"
    "  --simulate=P           開票進捗 P (0〜1) の模擬スナップショット\n"
    "  --check=FILE [--races] 結果ファイルの検証と議席・国民審査の集計\n"
    "  --rollup=FILE          市区町村単位 → 選挙区・都道府県単位に集約\n"
    "  --night                模擬開票を1分刻みで進めてイベントを表示\n"
    "  共通: --replay (実際の結果を再生), --seed=N\n"
    "\n"
    "usage: results_tool [--root=DIR] <mode>\n"
    "  --template             zeroed feed (SMD per district, PR/reviews per prefecture)\n"
    "  --simulate=P           simulated snapshot at night progress P in [0, 1]\n"
    "  --check=FILE [--races] validate a results file; seats and reviews\n"
    "  --rollup=FILE          aggregate unit-level entries to districts/prefectures\n"
    "  --night                step a simulated night and print its events\n"
    "  common: --replay (reveal the real results), --seed=N\n";

const char* StatusLabelJa(election::ResultsStatus s) {
  switch (s) {
    case election::ResultsStatus::kPreElection: return "開票前";
    case election::ResultsStatus::kCounting: return "開票中";
    case election::ResultsStatus::kFinal: return "確定";
  }
  return "";
}

// Display name of a race entry: SMD candidate name, or the list's party.
std::string EntryName(const election::ElectionData& data, const election::Race& race, int i) {
  if (i < 0 || i >= static_cast<int>(race.candidates.size())) return "-";
  const election::Candidate& c = race.candidates[i];
  if (race.is_pr()) return data.party(c.party).short_ja;
  return c.name_ja + "(" + data.party(c.party).short_ja + ")";
}

std::vector<std::string> PrefectureCodes(const geo::RegionTree& tree) {
  std::vector<std::string> out;
  for (int id : tree.nation().children) out.push_back(tree.region(id).code);
  return out;
}

bool LoadResults(const std::string& path, const election::ElectionData& data,
                 std::shared_ptr<election::ResultsSnapshot>* out, std::string* error) {
  std::string text;
  if (!election::ReadFile(path, &text)) {
    *error = "cannot read " + path;
    return false;
  }
  auto snap = std::make_shared<election::ResultsSnapshot>();
  if (!election::ParseResultsJson(text, data, snap.get(), error)) return false;
  *out = snap;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string root = ".";
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) root = ws;
  std::string mode, arg;
  bool replay = false, list_races = false;
  uint64_t seed = 20260208;
  const std::vector<std::string> args = tools::Utf8Args(argc, argv);
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a.rfind("--root=", 0) == 0) root = a.substr(7);
    else if (a == "--template") mode = "template";
    else if (a.rfind("--simulate=", 0) == 0) mode = "simulate", arg = a.substr(11);
    else if (a.rfind("--check=", 0) == 0) mode = "check", arg = a.substr(8);
    else if (a.rfind("--rollup=", 0) == 0) mode = "rollup", arg = a.substr(9);
    else if (a == "--night") mode = "night";
    else if (a == "--replay") replay = true;
    else if (a == "--races") list_races = true;
    else if (a.rfind("--seed=", 0) == 0) seed = std::strtoull(a.substr(7).c_str(), nullptr, 10);
    else if (a == "--help" || a == "-h") mode.clear(), i = args.size();
    else {
      std::fprintf(stderr, "不明なオプション / unknown flag: %s\n\n%s", a.c_str(), kUsage);
      return 2;
    }
  }
  if (mode.empty()) {
    std::fprintf(stderr, "%s", kUsage);
    return 2;
  }
  election::ElectionData data;
  std::string error;
  if (!data.Load(root + "/data/election/2026", &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  geo::RegionTree tree;
  std::string topo;
  if (!election::ReadFile(root + "/data/map/japan.topo.json", &topo) ||
      !tree.LoadTopoJson(topo, &error)) {
    std::fprintf(stderr, "地図を読み込めません / cannot load map: %s\n", error.c_str());
    return 1;
  }
  const std::vector<std::string> prefs = PrefectureCodes(tree);

  if (mode == "template") {
    election::ResultsSnapshot snap;
    snap.status = election::ResultsStatus::kPreElection;
    snap.source = "template";
    for (const election::Race& race : data.races()) {
      election::Tally t;
      t.votes.assign(race.candidates.size(), 0);
      if (race.is_smd()) {
        snap.races[race.id][race.region] = t;
      } else {
        for (const std::string& pref : race.prefs) snap.races[race.id][pref] = t;
      }
    }
    for (const auto& ref : data.info().referendums) {
      for (const std::string& pref : prefs) snap.referendums[ref.id][pref] = {};
    }
    std::cout << election::ResultsToJson(snap, data) << "\n";
    return 0;
  }

  // The real final results: replayed by --replay, and lending its counting
  // units and electorate sizes to the synthetic night.
  std::shared_ptr<election::ResultsSnapshot> reference;
  if (!LoadResults(root + "/data/election/2026/results.json", data, &reference, &error)) {
    if (replay) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    reference.reset();
  }
  const auto sim_mode = replay ? election::SimulatedResultsSource::Mode::kReplay
                               : election::SimulatedResultsSource::Mode::kSynthetic;

  if (mode == "night") {
    election::SimulatedResultsSource sim(&data, &tree, seed, 64, reference, sim_mode);
    election::EventTracker tracker(&tree);
    std::map<std::string, int> counts;
    const auto called = [&tracker](const election::Race& race) {
      return tracker.CalledCandidate(race.id);
    };
    for (int minute = 0; minute <= election::SimulatedResultsSource::kCountMinutes; ++minute) {
      auto snap = sim.SnapshotAtClock(minute);
      election::ResultsView view(&data, &tree, snap);
      const election::SeatSummary seats = election::ComputeSeats(data, view, called);
      const auto batch = tracker.Update(view, data.races(), &seats);
      for (const auto& e : batch.events) {
        counts[election::EventKey(e.type)]++;
        std::printf("%s %-18s %-16s %-20s %-20s 差 %-7lld %3.0f%%%s%s\n", e.time_label.c_str(),
                    election::EventKey(e.type), e.race->name_ja.c_str(),
                    EntryName(data, *e.race, e.leader).c_str(),
                    EntryName(data, *e.race, e.previous).c_str(), static_cast<long long>(e.margin),
                    e.progress * 100, e.zero_call ? "  [ゼロ打ち]" : "",
                    election::IsBreaking(e.type) ? "  [速報]" : "");
      }
    }
    for (const auto& [k, n] : counts) std::printf("%-20s %d\n", k.c_str(), n);
    std::printf("報告バッチ / batches: %d, 開票単位 / units: %d, 最終報告 / last report %s\n",
                sim.station_count(), sim.unit_count(),
                election::SimulatedResultsSource::ClockLabel(sim.last_report_minute()).c_str());
    return 0;
  }
  if (mode == "simulate") {
    election::SimulatedResultsSource sim(&data, &tree, seed, 64, reference, sim_mode);
    auto snap = sim.SnapshotAt(std::atof(arg.c_str()));
    std::cout << election::ResultsToJson(*snap, data) << "\n";
    return 0;
  }

  std::shared_ptr<election::ResultsSnapshot> snap;
  if (!LoadResults(arg, data, &snap, &error)) {
    std::fprintf(stderr, "無効なファイル / invalid: %s\n", error.c_str());
    return 1;
  }
  election::ResultsView view(&data, &tree, snap);

  if (mode == "rollup") {
    election::ResultsSnapshot out;
    out.status = snap->status;
    out.source = snap->source.empty() ? "rollup" : snap->source + " (rollup)";
    out.updated_at = snap->updated_at;
    out.simulated = snap->simulated;
    out.replay = snap->replay;
    out.declarations = snap->declarations;
    for (const election::Race& race : data.races()) {
      if (race.is_smd()) {
        const election::Tally& t = view.RaceTotal(race);
        if (t.has_data) out.races[race.id][race.region] = t;
      } else {
        for (const std::string& pref : race.prefs) {
          const election::Tally& t = view.RaceTally(race, tree.FindByCode(pref));
          if (t.has_data) out.races[race.id][pref] = t;
        }
      }
    }
    for (const auto& ref : data.info().referendums) {
      for (const std::string& pref : prefs) {
        const election::RefTally& t = view.ReferendumTally(ref.id, tree.FindByCode(pref));
        if (t.has_data) out.referendums[ref.id][pref] = t;
      }
    }
    std::cout << election::ResultsToJson(out, data) << "\n";
    return 0;
  }

  // --check
  std::printf("状態 status=%s simulated=%d replay=%d source=%s updated_at=%s\n",
              StatusLabelJa(snap->status), snap->simulated, snap->replay, snap->source.c_str(),
              snap->updated_at.c_str());
  int unknown_regions = 0, entries = 0;
  std::set<std::string> unknown_samples;
  for (const auto& [race_id, regions] : snap->races) {
    for (const auto& [code, t] : regions) {
      ++entries;
      if (tree.FindByCode(code) < 0) {
        ++unknown_regions;
        if (unknown_samples.size() < 5) unknown_samples.insert(race_id + "/" + code);
      }
    }
  }
  for (const auto& [ref_id, regions] : snap->referendums) {
    for (const auto& [code, t] : regions) {
      ++entries;
      if (tree.FindByCode(code) < 0) {
        ++unknown_regions;
        if (unknown_samples.size() < 5) unknown_samples.insert(ref_id + "/" + code);
      }
    }
  }
  std::printf("地域エントリ / region entries: %d, 不明な地域コード / unknown codes: %d", entries,
              unknown_regions);
  for (const std::string& s : unknown_samples) std::printf(" %s", s.c_str());
  std::printf("\n");

  if (list_races) {
    for (const election::Race& race : data.races()) {
      const election::Tally& t = view.RaceTotal(race);
      const std::vector<int> rank = t.Ranking();
      const int lead = t.Leader();
      const long long margin =
          rank.size() >= 2 ? static_cast<long long>(t.votes[rank[0]] - t.votes[rank[1]]) : 0;
      std::printf("%-12s %-16s 開票 %3.0f%% (%d/%d) 票 %-9lld 首位 %-24s 差 %lld\n",
                  race.id.c_str(), race.name_ja.c_str(), t.Progress() * 100, t.units_counted,
                  t.units_total, static_cast<long long>(t.TotalVotes()),
                  lead >= 0 ? EntryName(data, race, lead).c_str() : "-", margin);
    }
  }

  const election::SeatSummary seats = election::ComputeSeats(data, view);
  std::printf("\n比例ブロック / PR blocs:\n");
  for (const election::PrOutcome& pr : seats.pr) {
    std::string line;
    for (const election::PrListOutcome& l : pr.lists) {
      if (l.seats == 0) continue;
      line += " " + EntryName(data, *pr.race, l.list) + " " + std::to_string(l.seats);
    }
    std::printf("  %-20s 定数 %2d 開票 %3.0f%%%s%s\n", pr.race->name_ja.c_str(), pr.race->seats,
                pr.progress * 100, line.c_str(),
                pr.forfeited ? ("  (他党へ移譲 " + std::to_string(pr.forfeited) + ")").c_str()
                             : "");
  }
  std::printf("\n党派別議席 / seats by party (小選挙区 + 比例 = 計, 過半数 %d):\n", seats.majority);
  for (const election::PartySeats& p : seats.parties) {
    if (p.total() == 0) continue;
    std::printf("  %-12s %3d + %3d = %3d  (比例票 %lld)\n", data.party(p.party).short_ja.c_str(),
                p.smd, p.pr, p.total(), static_cast<long long>(p.pr_votes));
  }
  std::printf("  小選挙区 開票あり %d / 289, 比例 確定ブロック %d / 11\n", seats.smd_reporting,
              seats.pr_final);

  std::printf("\n国民審査 / national review:\n");
  const int nation = tree.FindByCode("JP") >= 0 ? tree.FindByCode("JP") : 0;
  for (const auto& ref : data.info().referendums) {
    const election::RefTally& t = view.ReferendumTally(ref.id, nation);
    std::printf("  %-10s 罷免を可とする %lld (%.2f%%) / 可としない %lld  開票 %3.0f%%  %s\n",
                ref.justice_ja.c_str(), static_cast<long long>(t.agree), t.AgreeShare() * 100,
                static_cast<long long>(t.disagree), t.Progress() * 100,
                t.Passes() ? "罷免" : "信任");
  }
  return 0;
}
