// Where live tallies come from.
//
//  * FileResultsSource: polls a results JSON file (see results.h for the
//    format) and reloads it whenever it changes. Point any feed/scraper at
//    that file, e.g. the converter in tools/.
//  * SimulatedResultsSource: an election night on a simulated clock, in one
//    of two clearly-labelled modes:
//      - kSynthetic (--simulate): a deterministic DEMO with random,
//        party-blind vote strengths. Not a forecast.
//      - kReplay (--replay): the real final results (a reference snapshot)
//        revealed batch by batch over the night; every batch sum converges
//        exactly to the official numbers.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/election/results.h"
#include "src/geo/region_tree.h"

namespace jpy::election {

class ResultsSource {
 public:
  virtual ~ResultsSource() = default;
  // Returns a new snapshot when the data changed since the last call.
  virtual std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) = 0;
  virtual std::string Describe() const = 0;
};

class FileResultsSource : public ResultsSource {
 public:
  FileResultsSource(const ElectionData* data, std::string path, double poll_interval_s = 1.0);
  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;
  const std::string& last_error() const { return last_error_; }

 private:
  const ElectionData* data_;
  std::string path_;
  double poll_interval_s_;
  double last_poll_ = -1e9;
  std::filesystem::file_time_type last_mtime_{};
  bool loaded_once_ = false;
  uint64_t version_ = 0;
  std::string last_error_;
};

// Simulated counting night. Polls close at 20:00; media call the safest
// districts at 20:00 sharp from exit polls (ゼロ打ち). Each counting unit
// (municipality / ward) then reports its district votes in 3-8 batches
// between ~21:00 and ~03:30, its party-list votes somewhat later and the
// national review last, until 04:00. Further 当選確実 calls follow as margins
// exceed the votes still out; winners give victory speeches (万歳) and
// runners-up concede. In kSynthetic mode some districts are built so that
// early-reporting units lean towards a different candidate than late ones,
// which produces genuine lead changes.
class SimulatedResultsSource : public ResultsSource {
 public:
  enum class Mode { kSynthetic, kReplay };

  static constexpr double kCountMinutes = 480;  // 20:00 -> 04:00

  static constexpr double kMinSpeed = 1;     // real time: 8 hours
  static constexpr double kMaxSpeed = 4096;  // ~7 seconds for the night

  // `speed`: simulated seconds per wall-clock second (x1 = real time).
  // `reference`: the final results. Replay reveals it; the synthetic mode
  // only borrows its set of counting units and their electorate sizes (so
  // the demo has realistic proportions) - never its votes.
  SimulatedResultsSource(const ElectionData* data, const geo::RegionTree* tree, uint64_t seed,
                         double speed = 64,
                         std::shared_ptr<const ResultsSnapshot> reference = nullptr,
                         Mode mode = Mode::kSynthetic);

  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;

  Mode mode() const { return mode_; }
  bool replay() const { return mode_ == Mode::kReplay; }

  // Snapshot at `minutes` after 20:00 (clamped to [0, kCountMinutes]).
  std::shared_ptr<ResultsSnapshot> SnapshotAtClock(double minutes) const;
  // Snapshot at counting progress `p` in [0, 1] of the night (time-based).
  std::shared_ptr<ResultsSnapshot> SnapshotAt(double p) const;

  void set_paused(bool paused) { paused_ = paused; }
  bool paused() const { return paused_; }
  void SeekClock(double minutes);  // also re-arms snapshot emission
  void SeekProgress(double p) { SeekClock(p * kCountMinutes); }
  double clock_minutes() const { return clock_; }
  double progress() const { return clock_ / kCountMinutes; }
  // Simulated seconds per real second, snapped to a power of two in
  // [kMinSpeed, kMaxSpeed]: x1, x2, x4, ... x4096.
  void set_speed(double s);
  double speed() const { return speed_; }
  void SpeedUp() { set_speed(speed_ * 2); }
  void SlowDown() { set_speed(speed_ / 2); }
  // Number of reported batches over the night (district, list and review).
  int station_count() const { return static_cast<int>(reports_.size()); }
  int unit_count() const { return static_cast<int>(units_.size()); }
  // Minute (after 20:00) by which every batch has reported.
  double last_report_minute() const { return last_report_; }
  // Minute of the last district (SMD) batch.
  double last_smd_minute() const { return last_smd_; }

  // District votes reported per `bucket_minutes` bucket from 20:00 up to
  // `until` (used to back-fill the inflow chart when starting mid-night).
  std::vector<int64_t> InflowHistory(double bucket_minutes, double until) const;

  // "21:42" for `minutes` after 20:00 (wraps past midnight: "01:30").
  static std::string ClockLabel(double minutes);
  // ISO-8601 JST timestamp for `minutes` after 20:00 on election day `date`.
  static std::string Timestamp(const std::string& date, double minutes);

 private:
  enum class Kind : uint8_t { kSmd, kPr, kReview };
  struct Report {
    float minute;    // report time, minutes after 20:00
    Kind kind;
    int race;        // index into data->races() (-1 for review)
    int region;      // counting unit (review: prefecture region id)
    int32_t eligible;
    int32_t cast;
    uint32_t votes_at;  // offset into votes_ (review: agree, disagree per justice)
  };
  struct CountingUnit {
    int region = -1;
    int smd = -1;  // race index
    int pr = -1;   // race index
    int pref = -1;  // prefecture region id
    int batches = 0;
  };
  void Build();
  void PlanDeclarations();

  struct PlannedDeclaration {
    float minute;
    int race;
    int candidate;
    Declaration::Type type;
  };
  std::vector<PlannedDeclaration> declarations_;

  const ElectionData* data_;
  const geo::RegionTree* tree_;
  uint64_t seed_;
  std::shared_ptr<const ResultsSnapshot> reference_;
  Mode mode_;
  bool paused_ = false;
  double speed_ = 64;
  double clock_ = 0;
  double last_report_ = 0;
  double last_smd_ = 0;
  double last_now_ = -1;
  double last_emit_ = -1e9;
  size_t emitted_reported_ = SIZE_MAX;
  size_t emitted_declared_ = SIZE_MAX;
  uint64_t version_ = 0;
  std::vector<CountingUnit> units_;
  std::vector<Report> reports_;  // sorted by report time
  std::vector<int32_t> votes_;
  std::vector<int> review_batches_;  // prefecture region id -> review batches
};

}  // namespace jpy::election
