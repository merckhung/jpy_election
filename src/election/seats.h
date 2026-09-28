// Seat computation for the House of Representatives (465 seats).
//
//  * Single-member districts (289): the leading candidate holds the seat;
//    it is "decided" once the count is complete or the race has been called
//    (当選確実).
//  * Proportional representation (176 seats in 11 blocs): D'Hondt per bloc
//    over the party-list votes, with a cap per list = its number of eligible
//    entries. A list entry is ineligible when it is struck off (a dual
//    candidate who wins - or currently leads - their district, or who got
//    less than 10% of the district's valid votes; list-only entries marked
//    `disqualified`). When a list runs out of eligible entries its surplus
//    seats go to the next-highest quotients of the other lists (as happened
//    in 2026 to the LDP in four blocs and to Team Mirai in Kinki).
//  * PR names: entries in list order (`no`); entries sharing a rank are
//    ordered by 惜敗率 (candidate votes / district winner votes x 100,
//    computed from the current SMD tallies), highest first.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "src/election/model.h"
#include "src/election/results.h"

namespace jpy::election {

struct SmdOutcome {
  const Race* race = nullptr;
  int leader = -1;     // candidate index (-1 = no votes and not called)
  int runner_up = -1;
  int64_t margin = 0;  // leader - runner-up votes
  int64_t valid = 0;   // valid votes counted
  double progress = 0;
  bool final = false;    // every unit counted
  bool called = false;   // called (当選確実) by the feed / tracker
  bool decided = false;  // final or called
};

struct PrListOutcome {
  int list = -1;  // index into race->candidates (the party list)
  int64_t votes = 0;
  int seats = 0;             // D'Hondt seats after caps
  int uncapped_seats = 0;    // plain D'Hondt seats (before caps)
  int cap = 0;               // eligible list entries
  std::vector<int> elected;  // indices into Candidate::list, in election order
};

struct PrOutcome {
  const Race* race = nullptr;
  std::vector<PrListOutcome> lists;  // same order as race->candidates
  int64_t valid = 0;
  double progress = 0;
  bool final = false;
  int forfeited = 0;  // seats moved to other lists because a list ran out
};

struct PartySeats {
  std::string party;
  int smd = 0;          // districts led (or won)
  int smd_decided = 0;  // districts won or called
  int pr = 0;           // projected PR seats
  int pr_decided = 0;   // PR seats in blocs whose count is complete
  int64_t pr_votes = 0;  // nationwide party-list votes
  int total() const { return smd + pr; }
  int decided() const { return smd_decided + pr_decided; }
};

struct SeatSummary {
  std::vector<SmdOutcome> smd;      // one per SMD race, data order
  std::vector<PrOutcome> pr;        // one per PR bloc, data order
  std::vector<PartySeats> parties;  // parties with seats or PR votes, by total desc
  int total_seats = 0;
  int majority = 0;
  int supermajority = 0;
  int smd_reporting = 0;  // districts with votes
  int smd_decided = 0;
  int pr_final = 0;       // blocs fully counted
  int64_t pr_votes = 0;   // nationwide valid party-list votes

  const SmdOutcome* Smd(const Race* race) const;
  const PrOutcome* Pr(const Race* race) const;
  const PartySeats* Party(std::string_view code) const;
};

// Returns the called candidate index of an SMD race, or -1.
using CalledFn = std::function<int(const Race&)>;

// Seat projection from the current tallies. `called` may be empty.
SeatSummary ComputeSeats(const ElectionData& data, ResultsView& view,
                         const CalledFn& called = nullptr);

// D'Hondt with per-list caps: `seats` seats over `votes`; a list never gets
// more than caps[i] (caps may be empty = uncapped). Ties go to the lower
// index. Returns seats per list.
std::vector<int> DHondt(const std::vector<int64_t>& votes, const std::vector<int>& caps, int seats);

// 惜敗率 (%) of SMD candidate `cand` in `t`: votes / winner votes x 100.
double Sekihairitsu(const Tally& t, int cand);

}  // namespace jpy::election
