// Turns successive results snapshots into election-night events: first
// returns, lead changes, media calls (当選確実), victory speeches and
// concessions, completed counts and PR seat shifts, plus the per-region vote
// deltas and lead flips the map animates. Works for any results source
// (live file feed, replay or simulation).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/election/results.h"
#include "src/election/seats.h"
#include "src/geo/region_tree.h"

namespace jpy::election {

// Election-night events. "Breaking" (速報) events are the ones that change the
// story of a race and get a banner plus a map call-out:
//   kLeadChange        the leader is overtaken (逆転)
//   kVictoryDeclared   the winner's victory speech at campaign HQ (万歳)
//   kConcession        the runner-up concedes (敗戦の弁)
//   kCalled            the race is called (当選確実): by a media call in the
//                      feed (often at 20:00 sharp from exit polls, "ゼロ打ち")
//                      or when the margin exceeds every vote still out
//   kIncumbentTrailing a sitting member (前) falls behind (前職劣勢)
// Regular updates get a call-out and a feed line only:
//   kFirstReturns      first votes in a race (開票開始)
//   kCloseRace         margin under 1% with 70%+ counted (大接戦)
//   kFinal             every unit counted (開票終了)
//   kPrSeats           a PR bloc's projected D'Hondt seats shift from one
//                      list to another (比例議席の変動); `leader` gains a seat
//                      from `previous`, `margin` = the gainer's new seat count
enum class EventType {
  kFirstReturns,
  kLeadChange,
  kVictoryDeclared,
  kConcession,
  kCalled,
  kIncumbentTrailing,
  kCloseRace,
  kFinal,
  kPrSeats,
};

bool IsBreaking(EventType t);
const char* EventKey(EventType t);  // i18n key suffix, e.g. "lead_change"

struct ElectionEvent {
  EventType type = EventType::kFirstReturns;
  const Race* race = nullptr;
  int leader = -1;    // candidate index (party-list index for PR races)
  int previous = -1;  // previous leader (lead change) / list losing a seat
  int64_t margin = 0;
  double progress = 0;     // share of units counted in the race
  std::string time_label;  // "21:42" (from the snapshot's updated_at)
  // Lead change against a candidate who had already been called/declared.
  bool after_declaration = false;
  // kCalled at the close of polls with (almost) nothing counted: ゼロ打ち.
  bool zero_call = false;
};

struct RegionDelta {
  int region = -1;
  int64_t votes_added = 0;
};

struct RegionFlip {
  int region = -1;
  const Race* race = nullptr;
  int leader = -1;
  int previous = -1;
};

struct EventBatch {
  std::vector<ElectionEvent> events;
  std::vector<RegionDelta> deltas;  // prefectures, districts and units with new SMD votes
  std::vector<RegionFlip> flips;    // districts / units whose SMD leader changed
  int64_t votes_added = 0;          // nationwide SMD votes added
  bool baseline = false;            // first snapshot: state recorded, no events
};

class EventTracker {
 public:
  explicit EventTracker(const geo::RegionTree* tree) : tree_(tree) {}

  // Compares `view` with the previous snapshot seen. `seats` (optional)
  // enables kPrSeats events.
  EventBatch Update(ResultsView& view, const std::vector<Race>& races,
                    const SeatSummary* seats = nullptr);

  // True once a race's winner is called (or the count is complete).
  bool IsCalled(const std::string& race_id) const;
  // The called candidate of a race, or -1.
  int CalledCandidate(const std::string& race_id) const;
  void Reset();

  // Projection rule: the leader's margin exceeds every vote still to come,
  // estimated from the average votes per counted unit (+15% safety).
  static bool Decided(const Tally& t);

 private:
  struct RegionState {
    int64_t total = 0;
    int leader = -1;
  };
  struct RaceState {
    int64_t total = 0;  // race-level votes at the previous update
    bool called = false;
    int called_candidate = -1;
    bool final = false;
    bool close_alerted = false;
    bool incumbent_alerted = false;
    int declared = -1;  // candidate who gave a victory speech
    int leader = -1;    // leader as reported in events (with hysteresis)
    size_t declarations_seen = 0;
    std::vector<int> pr_seats;  // last projected seats per list (PR)
  };
  const geo::RegionTree* tree_;
  bool has_baseline_ = false;
  std::unordered_map<int, RegionState> regions_;
  std::unordered_map<std::string, RaceState> races_;
};

// "21:42" from "2026-02-08T21:42:00+09:00"; "" when absent.
std::string TimeLabel(const std::string& updated_at);
// Minutes after the close of polls (20:00) for an updated_at timestamp, or -1.
// Times after midnight count on (00:30 -> 270).
double MinutesAfterClose(const std::string& updated_at);

}  // namespace jpy::election
