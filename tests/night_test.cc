// Election-night simulation / replay and event detection.
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/election/seats.h"
#include "src/geo/region_tree.h"
#include "tests/test_root.h"

namespace jpy::election {
namespace {

class NightTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    data_ = new ElectionData();
    std::string error;
    ASSERT_TRUE(data_->Load("data/election/2026", &error)) << error;
    std::string json;
    ASSERT_TRUE(ReadFile("data/map/japan.topo.json", &json));
    tree_ = new geo::RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
    std::string results;
    ASSERT_TRUE(ReadFile("data/election/2026/results.json", &results));
    auto snap = std::make_shared<ResultsSnapshot>();
    ASSERT_TRUE(ParseResultsJson(results, *data_, snap.get(), &error)) << error;
    final_ = new std::shared_ptr<const ResultsSnapshot>(snap);
  }
  static void TearDownTestSuite() {
    delete data_;
    delete tree_;
    delete final_;
  }
  static int SmdRaces() {
    int n = 0;
    for (const Race& r : data_->races()) n += r.is_smd();
    return n;
  }
  static ElectionData* data_;
  static geo::RegionTree* tree_;
  static std::shared_ptr<const ResultsSnapshot>* final_;
};

ElectionData* NightTest::data_ = nullptr;
geo::RegionTree* NightTest::tree_ = nullptr;
std::shared_ptr<const ResultsSnapshot>* NightTest::final_ = nullptr;

TEST_F(NightTest, SpeedSnapsToPowersOfTwo) {
  SimulatedResultsSource sim(data_, tree_, 1, 64);
  EXPECT_EQ(sim.speed(), 64);
  sim.set_speed(100);
  EXPECT_EQ(sim.speed(), 128);
  sim.set_speed(0.3);
  EXPECT_EQ(sim.speed(), 1);
  sim.set_speed(1e9);
  EXPECT_EQ(sim.speed(), 4096);
  sim.SlowDown();
  EXPECT_EQ(sim.speed(), 2048);
  sim.SpeedUp();
  EXPECT_EQ(sim.speed(), 4096);
}

TEST_F(NightTest, ClockAdvancesWithSpeed) {
  SimulatedResultsSource sim(data_, tree_, 1, 60);  // -> x64
  sim.Poll(0);
  sim.Poll(60);  // one real minute at x64 = 64 simulated minutes
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  sim.set_paused(true);
  sim.Poll(120);
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  EXPECT_EQ(SimulatedResultsSource::ClockLabel(64), "21:04");
  EXPECT_EQ(SimulatedResultsSource::ClockLabel(270), "00:30");
  EXPECT_EQ(SimulatedResultsSource::Timestamp("2026-02-08", 64), "2026-02-08T21:04:00+09:00");
  EXPECT_EQ(SimulatedResultsSource::Timestamp("2026-02-08", 270), "2026-02-09T00:30:00+09:00");
  EXPECT_EQ(SimulatedResultsSource::Timestamp("2026-02-28", 300), "2026-03-01T01:00:00+09:00");
}

TEST_F(NightTest, CountRunsFromCloseOfPollsToFourAm) {
  SimulatedResultsSource sim(data_, tree_, 20260208, 64, *final_);
  EXPECT_GT(sim.unit_count(), 1900);
  EXPECT_GT(sim.station_count(), 3 * 3 * 1900);
  EXPECT_LE(sim.last_report_minute(), SimulatedResultsSource::kCountMinutes);
  EXPECT_GT(sim.last_report_minute(), 400);  // the review tails towards 04:00
  EXPECT_GT(sim.last_smd_minute(), 300);     // district counts past 01:00
  EXPECT_LE(sim.last_smd_minute(), 465);
  EXPECT_EQ(sim.SnapshotAtClock(20)->status, ResultsStatus::kPreElection);
  EXPECT_EQ(sim.SnapshotAtClock(180)->status, ResultsStatus::kCounting);
  EXPECT_EQ(sim.SnapshotAtClock(480)->status, ResultsStatus::kFinal);
  EXPECT_EQ(sim.SnapshotAtClock(120)->updated_at, "2026-02-08T22:00:00+09:00");
  EXPECT_EQ(sim.SnapshotAtClock(300)->updated_at, "2026-02-09T01:00:00+09:00");
}

TEST_F(NightTest, WholeNightProducesTheExpectedStory) {
  SimulatedResultsSource sim(data_, tree_, 20260208, 64, *final_);
  EventTracker tracker(tree_);
  std::map<EventType, int> counts;
  std::set<std::string> called;
  int zero_calls = 0;
  for (int minute = 0; minute <= 480; minute += 2) {
    auto snap = sim.SnapshotAtClock(minute);
    ResultsView view(data_, tree_, snap);
    const SeatSummary seats = ComputeSeats(*data_, view, [&](const Race& r) {
      return tracker.CalledCandidate(r.id);
    });
    const EventBatch batch = tracker.Update(view, data_->races(), &seats);
    EXPECT_EQ(batch.baseline, minute == 0);
    for (const ElectionEvent& e : batch.events) {
      counts[e.type]++;
      if (e.type == EventType::kCalled) {
        EXPECT_TRUE(called.insert(e.race->id).second) << "called twice: " << e.race->id;
        zero_calls += e.zero_call;
      }
    }
  }
  const int races = static_cast<int>(data_->races().size());
  const int smd = SmdRaces();
  EXPECT_EQ(counts[EventType::kFirstReturns], races);
  EXPECT_EQ(counts[EventType::kFinal], races);
  EXPECT_EQ(counts[EventType::kCalled], smd);
  EXPECT_EQ(counts[EventType::kVictoryDeclared], smd);
  EXPECT_GE(counts[EventType::kLeadChange], 5);
  EXPECT_GE(counts[EventType::kConcession], smd / 2);
  EXPECT_GE(counts[EventType::kPrSeats], 5);
  EXPECT_GE(zero_calls, 20);   // safe districts called at 20:00
  EXPECT_LT(zero_calls, smd);  // but not all of them
}

TEST_F(NightTest, ReplayConvergesExactlyToTheOfficialResults) {
  SimulatedResultsSource replay(data_, tree_, 20260208, 64, *final_,
                                SimulatedResultsSource::Mode::kReplay);
  EXPECT_TRUE(replay.replay());
  auto done = replay.SnapshotAtClock(SimulatedResultsSource::kCountMinutes);
  EXPECT_TRUE(done->replay);
  EXPECT_TRUE(done->simulated);
  EXPECT_EQ(done->status, ResultsStatus::kFinal);
  ResultsView got(data_, tree_, done), want(data_, tree_, *final_);
  for (const Race& r : data_->races()) {
    const Tally& a = got.RaceTotal(r);
    const Tally& b = want.RaceTotal(r);
    EXPECT_EQ(a.votes, b.votes) << r.id;
    EXPECT_EQ(a.eligible, b.eligible) << r.id;
    EXPECT_EQ(a.ballots_cast, b.ballots_cast) << r.id;
  }
  // Per counting unit, too.
  for (const auto& [race_id, regions] : (*final_)->races) {
    for (const auto& [code, t] : regions) {
      const auto it = done->races.at(race_id).find(code);
      ASSERT_NE(it, done->races.at(race_id).end()) << race_id << " " << code;
      EXPECT_EQ(it->second.votes, t.votes) << race_id << " " << code;
    }
  }
  for (const Referendum& ref : data_->info().referendums) {
    for (int pref : tree_->nation().children) {
      const RefTally& a = got.ReferendumTally(ref.id, pref);
      const RefTally& b = want.ReferendumTally(ref.id, pref);
      EXPECT_EQ(a.agree, b.agree) << ref.id << " " << tree_->region(pref).code;
      EXPECT_EQ(a.disagree, b.disagree) << ref.id;
      EXPECT_EQ(a.eligible, b.eligible) << ref.id;
    }
  }
  const SeatSummary seats = ComputeSeats(*data_, got);
  EXPECT_EQ(seats.Party("LDP")->total(), 315);

  // Mid-night the replay is partial, and calls precede the end of the count.
  auto mid = replay.SnapshotAtClock(200);
  ResultsView vm(data_, tree_, mid);
  int64_t votes_mid = 0, votes_all = 0;
  for (const Race& r : data_->races()) {
    votes_mid += vm.RaceTotal(r).TotalVotes();
    votes_all += want.RaceTotal(r).TotalVotes();
  }
  EXPECT_GT(votes_mid, 0);
  EXPECT_LT(votes_mid, votes_all);
  int calls = 0;
  for (const Declaration& d : mid->declarations) calls += d.type == Declaration::Type::kCalled;
  EXPECT_GT(calls, 50);
  // Every call names the real winner.
  for (const Declaration& d : done->declarations) {
    if (d.type != Declaration::Type::kCalled) continue;
    const Race* race = data_->RaceById(d.race_id);
    ASSERT_NE(race, nullptr);
    const Tally& t = want.RaceTotal(*race);
    EXPECT_EQ(race->candidates[t.Leader()].id, d.candidate_id) << d.race_id;
  }
}

TEST_F(NightTest, DeclarationsRoundTripThroughJson) {
  SimulatedResultsSource sim(data_, tree_, 20260208, 64, *final_);
  auto snap = sim.SnapshotAtClock(400);
  ASSERT_FALSE(snap->declarations.empty());
  EXPECT_EQ(snap->declarations[0].type, Declaration::Type::kCalled);  // ゼロ打ち first
  const std::string json = ResultsToJson(*snap, *data_);
  ResultsSnapshot parsed;
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, &parsed, &error)) << error;
  ASSERT_EQ(parsed.declarations.size(), snap->declarations.size());
  for (size_t i = 0; i < parsed.declarations.size(); ++i) {
    EXPECT_EQ(parsed.declarations[i].candidate_id, snap->declarations[i].candidate_id);
    EXPECT_EQ(parsed.declarations[i].type, snap->declarations[i].type);
  }
}

TEST_F(NightTest, InflowHistoryMatchesSnapshots) {
  SimulatedResultsSource sim(data_, tree_, 5, 64, *final_);
  const auto buckets = sim.InflowHistory(5, 180);
  int64_t sum = 0;
  for (int64_t b : buckets) sum += b;
  auto snap = sim.SnapshotAtClock(180);
  ResultsView view(data_, tree_, snap);
  int64_t total = 0;
  for (const Race& r : data_->races()) {
    if (r.is_smd()) total += view.RaceTotal(r).TotalVotes();
  }
  EXPECT_EQ(sum, total);
  EXPECT_GT(total, 0);
}

TEST_F(NightTest, SyntheticWithoutReferenceStillWorks) {
  SimulatedResultsSource sim(data_, tree_, 3, 64);
  EXPECT_FALSE(sim.replay());
  auto done = sim.SnapshotAtClock(480);
  EXPECT_EQ(done->status, ResultsStatus::kFinal);
  ResultsView view(data_, tree_, done);
  const SeatSummary seats = ComputeSeats(*data_, view);
  int total = 0;
  for (const PartySeats& p : seats.parties) total += p.total();
  EXPECT_EQ(total, 465);
}

// Hand-made snapshots for the tracker rules (Tokyo 1st district).
class TrackerTest : public NightTest {
 protected:
  // a = 13-01-01 (元, LDP), b = 13-01-02 (前, CRA).
  std::shared_ptr<ResultsSnapshot> Snap(int64_t a, int64_t b, int counted, int total,
                                        const std::string& time) {
    auto s = std::make_shared<ResultsSnapshot>();
    const Race& race = *data_->SmdRaceForDistrict("13-01");
    Tally t;
    t.votes.assign(race.candidates.size(), 0);
    t.votes[race.CandidateIndex("13-01-01")] = a;
    t.votes[race.CandidateIndex("13-01-02")] = b;
    t.units_counted = counted;
    t.units_total = total;
    t.has_data = true;
    s->races[race.id]["13-01"] = t;
    s->updated_at = (time < "12:00" ? "2026-02-09T" : "2026-02-08T") + time + ":00+09:00";
    return s;
  }
  EventBatch Feed(EventTracker& tracker, std::shared_ptr<ResultsSnapshot> s) {
    ResultsView view(data_, tree_, s);
    return tracker.Update(view, data_->races());
  }
  int A() const { return data_->SmdRaceForDistrict("13-01")->CandidateIndex("13-01-01"); }
  int B() const { return data_->SmdRaceForDistrict("13-01")->CandidateIndex("13-01-02"); }
};

TEST_F(TrackerTest, LeadChangeNeedsAMargin) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "20:00"));  // baseline
  auto b = Feed(tracker, Snap(1000, 900, 10, 100, "21:30"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFirstReturns);
  EXPECT_EQ(b.events[0].time_label, "21:30");
  // Overtaken by 10 votes: below the hysteresis margin, no event.
  b = Feed(tracker, Snap(2000, 2010, 20, 100, "21:40"));
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kLeadChange);
  // Clear overtake: lead change.
  b = Feed(tracker, Snap(3000, 3500, 30, 100, "21:50"));
  bool lead_change = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kLeadChange) {
      lead_change = true;
      EXPECT_EQ(e.leader, B());
      EXPECT_EQ(e.previous, A());
      EXPECT_TRUE(IsBreaking(e.type));
    }
  }
  EXPECT_TRUE(lead_change);
}

TEST_F(TrackerTest, IncumbentTrailingCalledAndFinal) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "20:00"));
  Feed(tracker, Snap(100, 50, 1, 100, "21:20"));
  auto b = Feed(tracker, Snap(40000, 30000, 40, 100, "23:00"));
  std::set<EventType> types;
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kIncumbentTrailing));
  EXPECT_FALSE(types.count(EventType::kCalled));  // 60% still out
  b = Feed(tracker, Snap(90000, 30000, 90, 100, "00:30"));
  types.clear();
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kCalled));
  EXPECT_TRUE(tracker.IsCalled("13-01"));
  EXPECT_EQ(tracker.CalledCandidate("13-01"), A());
  b = Feed(tracker, Snap(99000, 33000, 100, 100, "01:00"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFinal);
}

TEST_F(TrackerTest, MediaCallsAndSpeechesBecomeEvents) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "20:00"));
  // ゼロ打ち: called at 20:00 before any vote is counted.
  auto s = Snap(0, 0, 0, 100, "20:00");
  s->declarations.push_back({"13-01", "13-01-01", Declaration::Type::kCalled,
                             "2026-02-08T20:00:00+09:00"});
  auto b = Feed(tracker, s);
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kCalled);
  EXPECT_TRUE(b.events[0].zero_call);
  EXPECT_EQ(b.events[0].leader, A());
  EXPECT_EQ(b.events[0].time_label, "20:00");
  EXPECT_EQ(tracker.CalledCandidate("13-01"), A());
  // Victory speech later; the same declarations again produce nothing new.
  auto s2 = Snap(5000, 3000, 50, 100, "21:05");
  s2->declarations = s->declarations;
  s2->declarations.push_back({"13-01", "13-01-01", Declaration::Type::kVictory,
                              "2026-02-08T21:05:00+09:00"});
  b = Feed(tracker, s2);
  int victory = 0, called = 0;
  for (const auto& e : b.events) {
    victory += e.type == EventType::kVictoryDeclared;
    called += e.type == EventType::kCalled;
  }
  EXPECT_EQ(victory, 1);
  EXPECT_EQ(called, 0);
  auto s3 = Snap(6000, 3500, 60, 100, "21:10");
  s3->declarations = s2->declarations;
  b = Feed(tracker, s3);
  for (const auto& e : b.events) {
    EXPECT_NE(e.type, EventType::kVictoryDeclared);
    EXPECT_NE(e.type, EventType::kCalled);
  }
}

TEST(Events, TimeLabels) {
  EXPECT_EQ(TimeLabel("2026-02-08T21:42:00+09:00"), "21:42");
  EXPECT_EQ(TimeLabel(""), "");
  EXPECT_DOUBLE_EQ(MinutesAfterClose("2026-02-08T21:42:00+09:00"), 102);
  EXPECT_DOUBLE_EQ(MinutesAfterClose("2026-02-09T00:30:00+09:00"), 270);
  EXPECT_DOUBLE_EQ(MinutesAfterClose("garbage"), -1);
}

}  // namespace
}  // namespace jpy::election
