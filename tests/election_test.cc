#include <filesystem>
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/election/seats.h"
#include "src/geo/region_tree.h"
#include "tests/test_root.h"

namespace jpy::election {
namespace {

class ElectionDataTest : public ::testing::Test {
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
  static ElectionData* data_;
  static geo::RegionTree* tree_;
  static std::shared_ptr<const ResultsSnapshot>* final_;
};

ElectionData* ElectionDataTest::data_ = nullptr;
geo::RegionTree* ElectionDataTest::tree_ = nullptr;
std::shared_ptr<const ResultsSnapshot>* ElectionDataTest::final_ = nullptr;

TEST_F(ElectionDataTest, RacesCandidatesAndSeats) {
  EXPECT_EQ(data_->info().date, "2026-02-08");
  EXPECT_EQ(data_->info().polls_open, "07:00");
  EXPECT_EQ(data_->info().polls_close, "20:00");
  EXPECT_EQ(data_->info().name_ja, "第51回衆議院議員総選挙");
  ASSERT_EQ(data_->races().size(), 300u);
  int smd = 0, pr = 0, pr_seats = 0;
  size_t smd_candidates = 0, list_entries = 0;
  std::set<std::string> ids;
  for (const Race& r : data_->races()) {
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(ids.insert(c.id).second) << "duplicate id " << c.id;
      EXPECT_FALSE(c.name_ja.empty()) << c.id;
    }
    if (r.is_smd()) {
      ++smd;
      EXPECT_EQ(r.seats, 1);
      EXPECT_GE(r.candidates.size(), 2u) << r.id;
      EXPECT_EQ(r.region, r.id);
      smd_candidates += r.candidates.size();
      for (const Candidate& c : r.candidates) EXPECT_EQ(c.id.substr(0, 5), r.id);
    } else {
      ++pr;
      pr_seats += r.seats;
      EXPECT_FALSE(r.prefs.empty()) << r.id;
      for (const Candidate& list : r.candidates) {
        EXPECT_GE(list.seats_won, 0) << list.id;
        list_entries += list.list.size();
      }
    }
  }
  EXPECT_EQ(smd, 289);
  EXPECT_EQ(pr, 11);
  EXPECT_EQ(pr_seats, 176);
  EXPECT_EQ(smd_candidates, 1119u);
  EXPECT_EQ(list_entries, 914u);
  EXPECT_EQ(data_->info().total_seats, 465);
  EXPECT_EQ(data_->info().majority, 233);
  EXPECT_EQ(data_->info().supermajority, 310);
  int bloc_seats = 0;
  for (const Bloc& b : data_->info().blocs) bloc_seats += b.seats;
  EXPECT_EQ(data_->info().blocs.size(), 11u);
  EXPECT_EQ(bloc_seats, 176);
}

TEST_F(ElectionDataTest, CandidateDetails) {
  const Race* tokyo1 = data_->SmdRaceForDistrict("13-01");
  ASSERT_NE(tokyo1, nullptr);
  EXPECT_EQ(tokyo1->name_ja, "東京1区");
  EXPECT_EQ(tokyo1->pref, "13");
  EXPECT_EQ(tokyo1->bloc, "tokyo");
  const Candidate* c = data_->CandidateById("13-01-02");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->name_ja, "海江田万里");
  EXPECT_EQ(c->status, "前");
  EXPECT_TRUE(c->incumbent);
  EXPECT_EQ(c->party, "CRA");
  EXPECT_FALSE(c->kana.empty());
  EXPECT_GT(c->age, 0);
  const Race* pr = data_->PrRaceForPref("13");
  ASSERT_NE(pr, nullptr);
  EXPECT_EQ(pr->id, "pr-tokyo");
  EXPECT_EQ(pr->seats, 19);
  const int ldp = pr->CandidateIndex("pr-tokyo-LDP");
  ASSERT_GE(ldp, 0);
  EXPECT_FALSE(pr->candidates[ldp].list.empty());
  EXPECT_EQ(pr->candidates[ldp].party, "LDP");
}

TEST_F(ElectionDataTest, EveryPartyIsKnownAndColoured) {
  std::set<std::string> codes;
  for (const Party& p : data_->parties()) {
    codes.insert(p.code);
    EXPECT_NE(p.color & 0x00FFFFFF, 0u) << p.code;
    EXPECT_FALSE(p.name_ja.empty()) << p.code;
    EXPECT_FALSE(p.short_ja.empty()) << p.code;
    EXPECT_FALSE(p.short_en.empty()) << p.code;
    EXPECT_FALSE(p.short_zh.empty()) << p.code;
  }
  for (const Race& r : data_->races()) {
    if (r.is_smd()) EXPECT_TRUE(codes.count(r.incumbent.party)) << r.id << " " << r.incumbent.party;
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(codes.count(c.party)) << c.id << " " << c.party;
    }
  }
  EXPECT_EQ(data_->party("LDP").name_ja, "自由民主党");
  EXPECT_EQ(data_->party("LDP").name_zh, "自由民主黨");
  EXPECT_EQ(data_->party("CRA").name_ja, "中道改革連合");
  EXPECT_EQ(data_->party("nope").code, "IND");
}

TEST_F(ElectionDataTest, EveryRaceMapsToTheMap) {
  for (const Race& r : data_->races()) {
    if (r.is_smd()) {
      const int id = tree_->FindByCode(r.region);
      ASSERT_GE(id, 0) << r.region;
      EXPECT_EQ(tree_->region(id).name_ja, r.name_ja);
      EXPECT_EQ(tree_->region(id).county_code, r.pref);
      EXPECT_EQ(data_->SmdRaceForDistrict(r.region), &r);
    } else {
      for (const std::string& p : r.prefs) {
        ASSERT_GE(tree_->FindByCode(p), 0) << p;
        EXPECT_EQ(data_->PrRaceForPref(p), &r);
      }
      EXPECT_EQ(data_->PrRaceForBloc(r.bloc), &r);
    }
  }
  for (int pref : tree_->nation().children) {
    EXPECT_NE(data_->PrRaceForPref(tree_->region(pref).code), nullptr) << tree_->region(pref).code;
  }
}

TEST_F(ElectionDataTest, ReviewAndOffices) {
  ASSERT_EQ(data_->info().referendums.size(), 2u);
  for (const Referendum& r : data_->info().referendums) {
    EXPECT_EQ(r.kind, "review");
    EXPECT_FALSE(r.justice_ja.empty());
    EXPECT_FALSE(r.justice_en.empty());
    EXPECT_FALSE(r.question_ja.empty());
    EXPECT_FALSE(r.question_zh.empty());
  }
  int seats = 0;
  for (const auto& o : data_->info().offices) {
    EXPECT_FALSE(o.name_ja.empty());
    EXPECT_FALSE(o.name_en.empty());
    seats += o.seats;
  }
  EXPECT_EQ(seats, data_->info().total_seats);
}

TEST_F(ElectionDataTest, ResultsAggregateWithinTheRacesCoverage) {
  const Race& tokyo1 = *data_->SmdRaceForDistrict("13-01");
  const Race& tokyo2 = *data_->SmdRaceForDistrict("13-02");
  const Race& pr = *data_->PrRaceForPref("13");
  const int district = tree_->FindByCode("13-01");
  const int pref = tree_->FindByCode("13");
  const int unit_a = tree_->region(district).children[0];
  const int unit_b = tree_->region(district).children[1];
  std::string json = R"({"status": "counting", "races": {"13-01": {"regions": {)";
  json += "\"" + tree_->region(unit_a).code +
          R"(": {"votes": {"13-01-01": 100, "13-01-02": 300}, "eligible": 1000,
                 "ballots_cast": 420, "units_counted": 1, "units_total": 1},)";
  json += "\"" + tree_->region(unit_b).code +
          R"(": {"votes": {"13-01-01": 50, "13-01-02": 10, "bogus": 99},
                 "units_counted": 0, "units_total": 1}}},
          "pr-tokyo": {"regions": {")" +
          tree_->region(unit_a).code + R"(": {"votes": {"pr-tokyo-LDP": 70, "pr-tokyo-CRA": 30}}}}},
          "referendums": {"review-takasu": {"regions": {"13": {"agree": 10, "disagree": 90}}}}})";
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, snap.get(), &error)) << error;
  EXPECT_EQ(snap->status, ResultsStatus::kCounting);
  ResultsView view(data_, tree_, snap);
  const Tally& t = view.RaceTally(tokyo1, pref);
  const int a = tokyo1.CandidateIndex("13-01-01");
  const int b = tokyo1.CandidateIndex("13-01-02");
  EXPECT_EQ(t.votes[a], 150);
  EXPECT_EQ(t.votes[b], 310);
  EXPECT_EQ(t.TotalVotes(), 460);
  EXPECT_EQ(t.Leader(), b);
  EXPECT_EQ(t.units_counted, 1);
  EXPECT_EQ(t.units_total, 2);
  EXPECT_NEAR(t.Share(b), 310.0 / 460.0, 1e-9);
  EXPECT_EQ(view.RaceTotal(tokyo1).TotalVotes(), 460);
  EXPECT_EQ(view.RaceTally(tokyo1, unit_b).Leader(), a);
  EXPECT_EQ(view.RaceTally(tokyo1, 0).TotalVotes(), 460);
  // Another district's race sees nothing here.
  EXPECT_EQ(view.RaceTally(tokyo2, district).TotalVotes(), 0);
  // PR bloc aggregates through districts and prefectures.
  EXPECT_EQ(view.RaceTally(pr, district).TotalVotes(), 100);
  EXPECT_EQ(view.RaceTotal(pr).TotalVotes(), 100);
  EXPECT_EQ(view.RaceTally(pr, tree_->FindByCode("27")).TotalVotes(), 0);
  // Lookups per region.
  EXPECT_EQ(view.SmdRaceForRegion(unit_a), &tokyo1);
  EXPECT_EQ(view.SmdRaceForRegion(district), &tokyo1);
  EXPECT_EQ(view.SmdRaceForRegion(pref), nullptr);
  EXPECT_EQ(view.PrRaceForRegion(unit_a), &pr);
  EXPECT_EQ(view.PrRaceForRegion(pref), &pr);
  EXPECT_EQ(view.PrRaceForRegion(0), nullptr);
  const auto covering = view.RacesCovering(district);
  ASSERT_EQ(covering.size(), 2u);
  EXPECT_EQ(covering[0], &tokyo1);
  EXPECT_EQ(covering[1], &pr);
  EXPECT_EQ(view.RacesCovering(pref).size(), 31u);  // 30 Tokyo districts + the bloc
  // The review is reported per prefecture; districts show that tally.
  EXPECT_EQ(view.ReferendumTally("review-takasu", district).agree, 10);
  EXPECT_TRUE(view.ReviewIsPrefectureLevel("review-takasu", district));
  EXPECT_FALSE(view.ReviewIsPrefectureLevel("review-takasu", pref));
  EXPECT_EQ(view.ReferendumTally("review-takasu", 0).disagree, 90);
}

TEST_F(ElectionDataTest, ResultsJsonRoundTrip) {
  SimulatedResultsSource sim(data_, tree_, 7, 60, *final_);
  auto snap = sim.SnapshotAt(0.6);
  const std::string json = ResultsToJson(*snap, *data_);
  auto parsed = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, parsed.get(), &error)) << error;
  ResultsView a(data_, tree_, snap), b(data_, tree_, parsed);
  for (const Race& r : data_->races()) {
    EXPECT_EQ(a.RaceTotal(r).votes, b.RaceTotal(r).votes) << r.id;
  }
  EXPECT_EQ(a.ReferendumTally("review-okino", 0).agree, b.ReferendumTally("review-okino", 0).agree);
  EXPECT_TRUE(parsed->simulated);
  EXPECT_FALSE(parsed->replay);
}

TEST_F(ElectionDataTest, SimulationIsDeterministicAndMonotonic) {
  SimulatedResultsSource sim(data_, tree_, 42, 60, *final_);
  auto early = sim.SnapshotAt(0.3);
  auto late = sim.SnapshotAt(0.9);
  auto again = sim.SnapshotAt(0.9);
  ResultsView ve(data_, tree_, early), vl(data_, tree_, late), va(data_, tree_, again);
  auto done = sim.SnapshotAt(1.0);
  ResultsView vd(data_, tree_, done);
  for (const Race& r : data_->races()) {
    EXPECT_LE(ve.RaceTotal(r).units_counted, vl.RaceTotal(r).units_counted);
    EXPECT_EQ(vl.RaceTotal(r).votes, va.RaceTotal(r).votes);
    EXPECT_EQ(vd.RaceTotal(r).units_counted, vd.RaceTotal(r).units_total) << r.id;
    EXPECT_GT(vd.RaceTotal(r).TotalVotes(), 0) << r.id;
  }
  EXPECT_EQ(done->status, ResultsStatus::kFinal);
  EXPECT_TRUE(done->simulated);
  // Synthetic votes are not the real ones.
  ResultsView real(data_, tree_, *final_);
  int same = 0;
  for (const Race& r : data_->races()) same += vd.RaceTotal(r).votes == real.RaceTotal(r).votes;
  EXPECT_EQ(same, 0);
}

// The official outcome: SMD winners and capped D'Hondt seats per bloc.
TEST_F(ElectionDataTest, FinalResultsReproduceTheOfficialSeats) {
  ResultsView view(data_, tree_, *final_);
  EXPECT_EQ(view.snapshot().status, ResultsStatus::kFinal);
  const SeatSummary seats = ComputeSeats(*data_, view);
  std::map<std::string, int> smd, pr, total;
  int winners_checked = 0;
  for (const SmdOutcome& o : seats.smd) {
    ASSERT_GE(o.leader, 0) << o.race->id;
    EXPECT_TRUE(o.final) << o.race->id;
    EXPECT_TRUE(o.decided) << o.race->id;
    ++smd[o.race->candidates[o.leader].party];
    // The winner is the candidate with an official 惜敗率 of 100 (a few
    // winners have no published figure).
    const double sekihairitsu = o.race->candidates[o.leader].sekihairitsu;
    if (sekihairitsu > 0) {
      EXPECT_DOUBLE_EQ(sekihairitsu, 100.0) << o.race->id;
      ++winners_checked;
    }
  }
  EXPECT_GT(winners_checked, 250);
  EXPECT_EQ(smd, (std::map<std::string, int>{{"LDP", 248}, {"JIP", 20}, {"DPFP", 8},
                                             {"CRA", 7}, {"IND", 5}, {"GENZEI", 1}}));
  int forfeited = 0;
  std::set<std::string> elected_smd_candidates;
  for (const PrOutcome& o : seats.pr) {
    EXPECT_TRUE(o.final);
    int sum = 0;
    forfeited += o.forfeited;
    for (const PrListOutcome& lo : o.lists) {
      const Candidate& list = o.race->candidates[lo.list];
      EXPECT_EQ(lo.seats, list.seats_won) << list.id;
      sum += lo.seats;
      pr[list.party] += lo.seats;
      std::set<int> official, ours(lo.elected.begin(), lo.elected.end());
      for (size_t k = 0; k < list.list.size(); ++k) {
        if (list.list[k].order > 0) official.insert(static_cast<int>(k));
      }
      EXPECT_EQ(ours, official) << list.id;
      for (int k : lo.elected) {
        if (!list.list[k].candidate.empty()) elected_smd_candidates.insert(list.list[k].candidate);
      }
    }
    EXPECT_EQ(sum, o.race->seats) << o.race->id;
  }
  EXPECT_GT(forfeited, 0);  // LDP (4 blocs) and Team Mirai (Kinki) ran out of names
  EXPECT_EQ(pr, (std::map<std::string, int>{{"LDP", 67}, {"CRA", 42}, {"DPFP", 20}, {"JIP", 16},
                                            {"SANSEI", 15}, {"MIRAI", 11}, {"JCP", 4},
                                            {"REIWA", 1}, {"SDP", 0}, {"CPJ", 0}, {"GENZEI", 0},
                                            {"EUTH", 0}}));
  // 比例復活: every revived district candidate is flagged pr_elected, and vice versa.
  for (const Race& r : data_->races()) {
    if (!r.is_smd()) continue;
    for (const Candidate& c : r.candidates) {
      EXPECT_EQ(c.pr_elected, elected_smd_candidates.count(c.id) > 0) << c.id;
    }
  }
  ASSERT_FALSE(seats.parties.empty());
  EXPECT_EQ(seats.parties[0].party, "LDP");
  EXPECT_EQ(seats.parties[0].total(), 315);
  EXPECT_EQ(seats.parties[0].decided(), 315);
  EXPECT_EQ(seats.Party("CRA")->total(), 49);
  EXPECT_EQ(seats.smd_decided, 289);
  EXPECT_EQ(seats.pr_final, 11);
  int all = 0;
  for (const PartySeats& p : seats.parties) all += p.total();
  EXPECT_EQ(all, 465);
}

TEST_F(ElectionDataTest, SekihairitsuMatchesTheOfficialFigures) {
  ResultsView view(data_, tree_, *final_);
  int checked = 0;
  for (const Race& r : data_->races()) {
    if (!r.is_smd()) continue;
    const Tally& t = view.RaceTotal(r);
    for (size_t i = 0; i < r.candidates.size(); ++i) {
      if (r.candidates[i].sekihairitsu <= 0) continue;
      EXPECT_NEAR(Sekihairitsu(t, static_cast<int>(i)), r.candidates[i].sekihairitsu, 0.01)
          << r.candidates[i].id;
      ++checked;
    }
  }
  EXPECT_GT(checked, 500);
}

TEST_F(ElectionDataTest, CalledCandidatesHoldTheirSeat) {
  // Nothing counted yet, one district called from exit polls (ゼロ打ち).
  auto empty = std::make_shared<ResultsSnapshot>();
  ResultsView view(data_, tree_, empty);
  const Race* tokyo1 = data_->SmdRaceForDistrict("13-01");
  const SeatSummary seats = ComputeSeats(*data_, view, [&](const Race& r) {
    return &r == tokyo1 ? tokyo1->CandidateIndex("13-01-01") : -1;
  });
  const SmdOutcome* o = seats.Smd(tokyo1);
  ASSERT_NE(o, nullptr);
  EXPECT_TRUE(o->decided);
  EXPECT_EQ(o->leader, tokyo1->CandidateIndex("13-01-01"));
  EXPECT_EQ(seats.smd_decided, 1);
  ASSERT_EQ(seats.parties.size(), 1u);
  EXPECT_EQ(seats.parties[0].party, "LDP");
  EXPECT_EQ(seats.parties[0].smd, 1);
  for (const PrOutcome& p : seats.pr) {
    for (const PrListOutcome& lo : p.lists) EXPECT_EQ(lo.seats, 0);
  }
}

TEST(DHondt, AllocatesAndRespectsCaps) {
  EXPECT_EQ(DHondt({1000, 800, 300}, {}, 5), (std::vector<int>{3, 2, 0}));
  // A list that runs out of names passes its seats to the next quotients.
  EXPECT_EQ(DHondt({1000, 800, 300}, {1, 5, 5}, 5), (std::vector<int>{1, 3, 1}));
  EXPECT_EQ(DHondt({0, 0}, {}, 3), (std::vector<int>{0, 0}));
  EXPECT_EQ(DHondt({500, 500}, {}, 1), (std::vector<int>{1, 0}));  // tie: lower index
}

TEST(RefTally, DismissedWhenAgreeOutnumbersDisagree) {
  RefTally t;
  t.eligible = 1000;
  t.agree = 100;
  t.disagree = 500;
  EXPECT_FALSE(t.Passes());
  EXPECT_NEAR(t.AgreeShare(), 100.0 / 600.0, 1e-12);
  t.agree = 501;
  EXPECT_TRUE(t.Passes());
}

TEST(Color, ParsesHex) {
  EXPECT_EQ(ParseColor("#1A48B5", 0), 0xFF1A48B5u);
  EXPECT_EQ(ParseColor("1A48B5", 7), 7u);
  EXPECT_EQ(ParseColor("#GG0000", 7), 7u);
}

}  // namespace
}  // namespace jpy::election
