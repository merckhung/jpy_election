// Vote tallies: snapshot parsing, and aggregation up the region hierarchy.
//
// Results file format (jpy_election.results/v1), all counts optional:
// {
//   "schema": "jpy_election.results/v1",
//   "status": "pre-election" | "counting" | "final",
//   "source": "free text", "updated_at": "ISO-8601",
//   "races": {
//     "13-01":    { "regions": { "<unit code>": { "votes": {"13-01-02": 123, ...},
//                                "eligible": 0, "ballots_cast": 0,
//                                "units_counted": 0, "units_total": 0 } } },
//     "pr-tokyo": { "regions": { "<unit code>": { "votes": {"pr-tokyo-LDP": 456, ...}, ... } } } },
//   "declarations": [   // media calls and campaign statements
//     { "race": "13-01", "candidate": "13-01-02",
//       "type": "called" | "victory" | "concede", "time": "2026-02-08T20:00:00+09:00" } ],
//   "referendums": {    // national review of Supreme Court justices
//     "review-takasu": { "regions": { "<prefecture code>": { "agree": 0, "disagree": 0,
//                  "eligible": 0, "ballots_cast": 0,
//                  "units_counted": 0, "units_total": 0 } } } }
// }
// Region codes are the japan.topo.json codes: "JP", prefecture "13", district
// "13-01" or counting unit "13101" / "13111_03". A region without an explicit
// entry is the sum of its children (restricted to the part the race covers),
// so feeds may report at any granularity.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace jpy::election {

struct Tally {
  std::vector<int64_t> votes;  // indexed like Race::candidates (party lists for PR)
  int64_t eligible = 0;
  int64_t ballots_cast = 0;
  int units_counted = 0;  // counting units (or batches of one) counted
  int units_total = 0;
  bool has_data = false;

  int64_t TotalVotes() const;
  // Candidate index with most votes, or -1 when no votes.
  int Leader() const;
  // Candidate indices sorted by votes (desc), ties by index.
  std::vector<int> Ranking() const;
  double Share(int candidate) const;
  double Turnout() const;   // ballots_cast / eligible, or 0
  double Progress() const;  // units_counted / units_total, or 0
  void Add(const Tally& other);
};

// National review tally for one justice: agree = marked "×" (罷免を可とする),
// disagree = left blank (罷免を可としない).
struct RefTally {
  int64_t agree = 0;
  int64_t disagree = 0;
  int64_t eligible = 0;
  int64_t ballots_cast = 0;
  int units_counted = 0;
  int units_total = 0;
  bool has_data = false;

  void Add(const RefTally& other);
  double Progress() const;
  double AgreeShare() const;  // dismiss share of valid review ballots, or 0
  // True when the justice is dismissed: "dismiss" marks outnumber the rest
  // (the turnout floor of 1% of the electorate is always met in practice).
  bool Passes() const;
};

enum class ResultsStatus { kPreElection, kCounting, kFinal };

const char* StatusLabelZh(ResultsStatus s);

// Statements independent of the count: a media call (当選確実, often at 20:00
// sharp from exit polls - "ゼロ打ち"), a winner's victory speech (万歳) or a
// loser's concession (敗戦の弁).
struct Declaration {
  enum class Type { kVictory, kConcede, kCalled };
  std::string race_id;
  std::string candidate_id;
  Type type = Type::kVictory;
  std::string time;  // ISO-8601
};

struct ResultsSnapshot {
  ResultsStatus status = ResultsStatus::kPreElection;
  std::string source;
  std::string updated_at;
  bool simulated = false;  // synthetic or replayed timing: never live data
  bool replay = false;     // real final numbers revealed on a simulated clock
  uint64_t version = 0;
  // race id -> region code -> tally
  std::unordered_map<std::string, std::unordered_map<std::string, Tally>> races;
  // review id -> region code -> tally
  std::unordered_map<std::string, std::unordered_map<std::string, RefTally>> referendums;
  std::vector<Declaration> declarations;
};

bool ParseResultsJson(std::string_view json, const ElectionData& data, ResultsSnapshot* out,
                      std::string* error);

std::string ResultsToJson(const ResultsSnapshot& snap, const ElectionData& data);

// Read-only aggregated view over a snapshot (memoised).
class ResultsView {
 public:
  ResultsView(const ElectionData* data, const geo::RegionTree* tree,
              std::shared_ptr<const ResultsSnapshot> snapshot);

  const ResultsSnapshot& snapshot() const { return *snapshot_; }
  const ElectionData& data() const { return *data_; }
  const geo::RegionTree& tree() const { return *tree_; }

  // Tally of `race` restricted to `region_id`: only the part of the region
  // the race covers is counted (an SMD: its district; a PR bloc: its
  // prefectures). Regions the race does not cover yield an empty tally.
  const Tally& RaceTally(const Race& race, int region_id);
  // The race's whole tally (its district, or the bloc for PR).
  const Tally& RaceTotal(const Race& race);
  // Region id where a race is decided: the district (SMD) or the nation (PR).
  int RaceRegion(const Race& race) const;
  static bool Covers(const Race& race, const geo::Region& region);

  // Review tally at a region. Feeds report the review per prefecture, so
  // districts and units without their own entry return their prefecture's
  // tally (see ReviewIsPrefectureLevel).
  const RefTally& ReferendumTally(const std::string& ref_id, int region_id);
  bool ReviewIsPrefectureLevel(const std::string& ref_id, int region_id) const;

  // The SMD race of a district or counting unit (nullptr for prefectures and
  // the nation, which span several districts).
  const Race* SmdRaceForRegion(int region_id) const;
  // Kept from the Taiwanese original: same as SmdRaceForRegion.
  const Race* RaceForRegion(int region_id) const { return SmdRaceForRegion(region_id); }
  // The PR bloc race whose ballots are cast in the region (nullptr for the
  // nation, which spans all 11 blocs).
  const Race* PrRaceForRegion(int region_id) const;
  // Every race with ballots in the region: its SMD(s) then its PR bloc(s).
  std::vector<const Race*> RacesCovering(int region_id) const;

 private:
  const ElectionData* data_;
  const geo::RegionTree* tree_;
  std::shared_ptr<const ResultsSnapshot> snapshot_;
  std::unordered_map<std::string, std::unordered_map<int, Tally>> race_memo_;
  std::unordered_map<std::string, std::unordered_map<int, RefTally>> ref_memo_;
};

}  // namespace jpy::election
