#include "src/ui/dashboard.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "src/election/events.h"
#include "src/election/results_source.h"

namespace jpy::ui {

using election::Candidate;
using election::ListEntry;
using election::Party;
using election::Race;
using election::RefTally;
using election::ResultsStatus;
using election::Tally;

namespace {

constexpr SkColor kPanel = SkColorSetARGB(222, 10, 20, 33);
constexpr SkColor kPanelBorder = SkColorSetARGB(40, 255, 255, 255);
constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kAccent = SkColorSetRGB(245, 197, 66);
constexpr SkColor kTrack = SkColorSetARGB(60, 255, 255, 255);
constexpr uint32_t kNeutral = 0xFF2B3D52;
constexpr uint32_t kCalledRed = 0xFFD63031;  // 当確
constexpr uint32_t kWonGreen = 0xFF2E9E5B;   // 当選
constexpr uint32_t kLeadAmber = 0xFFE08E0B;
constexpr uint32_t kRevived = 0xFF8E44AD;    // 比例復活
// Margin scale: close (red) -> middle (pale) -> safe (blue).
constexpr uint32_t kClose = 0xFFE4572E;
constexpr uint32_t kMid = 0xFFE9D8A6;
constexpr uint32_t kSafe = 0xFF2A7AB8;
constexpr double kMarginSafe = 0.30;  // lead of 30 points or more = fully "safe"
// Turnout scale.
constexpr uint32_t kTurnoutColor = 0xFF2EC4B6;
constexpr double kTurnoutLo = 0.45, kTurnoutHi = 0.70;
// National review: dismiss share scale.
constexpr uint32_t kDismiss = 0xFFE4572E;
constexpr uint32_t kRetain = 0xFF3FA7D6;
constexpr double kDismissLo = 0.05, kDismissHi = 0.25;

SkPaint Fill(SkColor c) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  return p;
}

void Panel(SkCanvas* c, const SkRect& r, float radius) {
  c->drawRRect(SkRRect::MakeRectXY(r, radius, radius), Fill(kPanel));
  SkPaint border = Fill(kPanelBorder);
  border.setStyle(SkPaint::kStroke_Style);
  border.setStrokeWidth(1);
  c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.5f, 0.5f), radius, radius), border);
}

void Bar(SkCanvas* c, const SkRect& r, float fraction, SkColor color, SkColor track = kTrack) {
  const float rad = r.height() * 0.5f;
  c->drawRRect(SkRRect::MakeRectXY(r, rad, rad), Fill(track));
  fraction = std::clamp(fraction, 0.f, 1.f);
  if (fraction <= 0) return;
  SkRect f = r;
  f.fRight = r.fLeft + std::max(r.height(), r.width() * fraction);
  c->drawRRect(SkRRect::MakeRectXY(f, rad, rad), Fill(color));
}

void Separator(SkCanvas* c, float x, float y, float w) {
  c->drawRect(SkRect::MakeXYWH(x, y, w, 1), Fill(SkColorSetARGB(36, 255, 255, 255)));
}

std::string Percent(double v, int decimals = 1) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f%%", decimals, v * 100.0);
  return buf;
}

std::string Fixed(double v, int decimals) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

bool HasVotes(const Tally& t) { return t.TotalVotes() > 0; }
bool Complete(const Tally& t) { return t.units_total > 0 && t.units_counted >= t.units_total; }
float ProgressFactor(double progress) {
  return 0.6f + 0.4f * static_cast<float>(std::clamp(progress, 0.0, 1.0));
}

// Final official numbers are on screen (not a simulation), so the official
// per-candidate fields (惜敗率, 比例復活, PR election order) apply.
bool OfficialFinal(const DashboardModel& m) {
  const auto& s = m.results->snapshot();
  return s.status == ResultsStatus::kFinal && (!s.simulated || s.replay);
}

uint32_t MarginColor(double margin) {
  const float t = static_cast<float>(std::clamp(margin / kMarginSafe, 0.0, 1.0));
  return t < 0.5f ? MixColor(kClose, kMid, t * 2) : MixColor(kMid, kSafe, (t - 0.5f) * 2);
}

// Lead of the leader over the runner-up, as a share of the valid vote.
double MarginShare(const Tally& t) {
  const std::vector<int> rank = t.Ranking();
  if (rank.empty() || !HasVotes(t)) return 0;
  return t.Share(rank[0]) - (rank.size() > 1 ? t.Share(rank[1]) : 0.0);
}

int CalledOf(const DashboardModel& m, const Race& race) {
  if (!m.race_status) return -1;
  auto it = m.race_status->find(race.id);
  return it == m.race_status->end() ? -1 : it->second.called;
}

// SMD leader in `region` (district or unit). At district level a media call
// (当選確実, possibly before any votes: ゼロ打ち) takes precedence.
int SmdLeader(const DashboardModel& m, const Race& race, int region, const Tally** tally) {
  const Tally& t = m.results->RaceTally(race, region);
  if (tally) *tally = &t;
  if (m.tree->region(region).level == geo::Level::kTown) {
    const int called = CalledOf(m, race);
    if (called >= 0 && called < static_cast<int>(race.candidates.size())) return called;
  }
  return HasVotes(t) ? t.Leader() : -1;
}

// Districts of a prefecture led per party (plurality colouring, tooltips).
struct PrefSummary {
  std::vector<std::pair<std::string, int>> parties;  // sorted by count desc
  int districts = 0;
  int with_leader = 0;
  double progress = 0;  // mean district progress
  double margin = 0;    // mean lead (districts with votes)
  int with_votes = 0;
  int64_t eligible = 0, ballots = 0;
};

PrefSummary SummarizePref(const DashboardModel& m, const geo::Region& pref) {
  PrefSummary s;
  std::map<std::string, int> count;
  double progress = 0, margin = 0;
  for (int d : pref.children) {
    const Race* race = m.results->SmdRaceForRegion(d);
    if (!race) continue;
    ++s.districts;
    const Tally* t = nullptr;
    const int lead = SmdLeader(m, *race, d, &t);
    progress += t->Progress();
    s.eligible += t->eligible;
    s.ballots += t->ballots_cast;
    if (HasVotes(*t)) {
      ++s.with_votes;
      margin += MarginShare(*t);
    }
    if (lead >= 0) {
      ++s.with_leader;
      count[race->candidates[lead].party]++;
    }
  }
  if (s.districts) s.progress = progress / s.districts;
  if (s.with_votes) s.margin = margin / s.with_votes;
  s.parties.assign(count.begin(), count.end());
  std::stable_sort(s.parties.begin(), s.parties.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  return s;
}

// Current (projected) 比例復活 of an SMD candidate from the D'Hondt lists.
bool RevivedNow(const DashboardModel& m, const Race& race, const Candidate& cand) {
  if (!m.seats || !cand.dual) return false;
  const Race* pr = m.data->PrRaceForBloc(race.bloc);
  if (!pr) return false;
  const election::PrOutcome* o = m.seats->Pr(pr);
  if (!o) return false;
  for (const auto& lo : o->lists) {
    if (lo.list < 0 || lo.list >= static_cast<int>(pr->candidates.size())) continue;
    const Candidate& list = pr->candidates[lo.list];
    if (list.party != cand.party) continue;
    for (int k : lo.elected) {
      if (k >= 0 && k < static_cast<int>(list.list.size()) && list.list[k].candidate == cand.id) {
        return true;
      }
    }
  }
  return false;
}

// Elected entries of a PR list (indices into Candidate::list, in order).
std::vector<int> ElectedEntries(const DashboardModel& m, const Race& pr, int list_index,
                                bool official) {
  std::vector<int> out;
  const Candidate& list = pr.candidates[list_index];
  if (official) {
    std::vector<std::pair<int, int>> by_order;
    for (size_t k = 0; k < list.list.size(); ++k) {
      if (list.list[k].order > 0) by_order.push_back({list.list[k].order, static_cast<int>(k)});
    }
    std::sort(by_order.begin(), by_order.end());
    for (const auto& [order, k] : by_order) out.push_back(k);
    if (!out.empty() || list.seats_won == 0) return out;
  }
  if (!m.seats) return out;
  if (const election::PrOutcome* o = m.seats->Pr(&pr)) {
    if (list_index < static_cast<int>(o->lists.size())) out = o->lists[list_index].elected;
  }
  return out;
}

int ListSeats(const DashboardModel& m, const Race& pr, int list_index, bool official) {
  const Candidate& list = pr.candidates[list_index];
  if (official && list.seats_won >= 0) return list.seats_won;
  if (!m.seats) return 0;
  if (const election::PrOutcome* o = m.seats->Pr(&pr)) {
    if (list_index < static_cast<int>(o->lists.size())) return o->lists[list_index].seats;
  }
  return 0;
}

// "2026-02-08" -> "2026年2月8日" / "Feb 8, 2026".
std::string FormatDate(Lang lang, const std::string& iso) {
  int y = 0, mo = 0, d = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &mo, &d) != 3 || mo < 1 || mo > 12) return iso;
  char buf[48];
  if (lang == Lang::kEn) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::snprintf(buf, sizeof buf, "%s %d, %d", kMonths[mo - 1], d, y);
  } else {
    std::snprintf(buf, sizeof buf, "%d年%d月%d日", y, mo, d);
  }
  return buf;
}

int ParseHm(const std::string& hm, int fallback_minutes) {
  int h = 0, mi = 0;
  if (std::sscanf(hm.c_str(), "%d:%d", &h, &mi) != 2) return fallback_minutes;
  return h * 60 + mi;
}

std::string HoursMinutes(int64_t seconds) {
  seconds = std::max<int64_t>(0, seconds);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld:%02lld", static_cast<long long>(seconds / 3600),
                static_cast<long long>((seconds / 60) % 60));
  return buf;
}

// Pre-election countdown, computed in Japan Standard Time (UTC+9, no DST)
// regardless of the machine's time zone.
struct Countdown {
  enum class Phase { kBefore, kElectionMorning, kVoting, kClosed } phase = Phase::kBefore;
  int days = 0;             // calendar days to election day (kBefore)
  int64_t seconds = 0;      // to polls opening (morning) / closing (voting)
  std::string now_label;    // "2/7 12:00" (JST)
};

Countdown ComputeCountdown(const election::ElectionInfo& info,
                           std::chrono::system_clock::time_point now) {
  Countdown cd;
  int y = 0, mo = 0, d = 0;
  if (std::sscanf(info.date.c_str(), "%d-%d-%d", &y, &mo, &d) != 3) return cd;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  // All times below are "JST seconds": UTC epoch seconds + 9 h.
#ifdef _WIN32
  const int64_t day_start = static_cast<int64_t>(_mkgmtime(&tm));
#else
  const int64_t day_start = static_cast<int64_t>(timegm(&tm));
#endif
  const int64_t open = day_start + ParseHm(info.polls_open, 7 * 60) * 60;
  const int64_t close = day_start + ParseHm(info.polls_close, 20 * 60) * 60;
  const int64_t jst =
      static_cast<int64_t>(std::chrono::system_clock::to_time_t(now)) + 9 * 3600;
  const int64_t today = jst - ((jst % 86400) + 86400) % 86400;
  {
    const std::time_t tt = static_cast<std::time_t>(jst);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &tt);
#else
    gmtime_r(&tt, &parts);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d/%d %02d:%02d", parts.tm_mon + 1, parts.tm_mday,
                  parts.tm_hour, parts.tm_min);
    cd.now_label = buf;
  }
  if (jst < day_start) {
    cd.phase = Countdown::Phase::kBefore;
    cd.days = static_cast<int>((day_start - today) / 86400);
  } else if (jst < open) {
    cd.phase = Countdown::Phase::kElectionMorning;
    cd.seconds = open - jst;
  } else if (jst < close) {
    cd.phase = Countdown::Phase::kVoting;
    cd.seconds = close - jst;
  } else {
    cd.phase = Countdown::Phase::kClosed;
  }
  return cd;
}

// Red seal "当選" / "当確" slamming down (scales in with a flash, then rests).
void Seal(SkCanvas* c, const Fonts* fonts, const std::string& text, float cx, float cy, float size,
          float age, uint32_t color) {
  const float t = std::clamp(age / 0.35f, 0.f, 1.f);
  const float ease = 1 - (1 - t) * (1 - t) * (1 - t);
  const float scale = 1 + (1 - ease) * 1.2f;
  const float alpha = std::min(1.f, 0.2f + t);
  const SkColor col = SkColorSetA(color, static_cast<U8CPU>(235 * alpha));
  c->save();
  c->translate(cx, cy);
  c->rotate(-14);
  c->scale(scale, scale);
  const float r = size / 2;
  SkPaint ring = Fill(col);
  ring.setStyle(SkPaint::kStroke_Style);
  ring.setStrokeWidth(size * 0.07f);
  c->drawCircle(0, 0, r, ring);
  ring.setStrokeWidth(size * 0.025f);
  c->drawCircle(0, 0, r * 0.82f, ring);
  const SkFont f = fonts->Bold(size * 0.34f);
  const float tw = TextWidth(f, text);
  SkFont fit = f;
  if (tw > size * 0.78f) fit = fonts->Bold(size * 0.34f * size * 0.78f / tw);
  DrawText(c, text, 0, size * 0.12f, fit, col, Align::kCenter);
  c->restore();
  if (age < 0.6f) {
    c->drawCircle(cx, cy, size * (0.5f + age * 1.5f),
                  Fill(SkColorSetA(kAccent, static_cast<U8CPU>(255 * (0.6f - age) * 0.8f))));
  }
}

// Map attribution: the source names from data/map/SOURCES.txt (numbered
// items "1. Label: Source name, details..."), or its first line.
std::string MapCredits(const std::string& root) {
  static std::mutex mu;
  static std::map<std::string, std::string> cache;
  std::lock_guard<std::mutex> lock(mu);
  if (auto it = cache.find(root); it != cache.end()) return it->second;
  std::ifstream in(root + "/data/map/SOURCES.txt", std::ios::binary);
  std::string result;
  if (in) {
    std::vector<std::string> items;
    std::string first, line, current;
    bool in_item = false;
    auto trim = [](std::string s) {
      const size_t a = s.find_first_not_of(" \t\r\n");
      if (a == std::string::npos) return std::string();
      const size_t b = s.find_last_not_of(" \t\r\n");
      return s.substr(a, b - a + 1);
    };
    auto flush = [&] {
      if (!in_item) return;
      std::string s = current;
      const size_t colon = s.find(": ");
      if (colon != std::string::npos) s = s.substr(colon + 2);
      const size_t cut = s.find_first_of(",(");
      if (cut != std::string::npos) s = s.substr(0, cut);
      s = trim(s);
      if (!s.empty()) items.push_back(s);
      in_item = false;
      current.clear();
    };
    while (std::getline(in, line)) {
      const std::string t = trim(line);
      if (t.empty()) {
        flush();
        continue;
      }
      if (first.empty() && t.find("===") == std::string::npos) first = t;
      if (t.size() > 2 && std::isdigit(static_cast<unsigned char>(t[0])) && t[1] == '.' &&
          t[2] == ' ') {
        flush();
        in_item = true;
        current = t.substr(3);
      } else if (in_item) {
        current += " " + t;
      }
    }
    flush();
    if (items.empty()) {
      result = first;
    } else {
      for (const std::string& s : items) result += (result.empty() ? "" : " · ") + s;
    }
  }
  cache[root] = result;
  return result;
}

}  // namespace

const char* ColorModeName(Lang lang, ColorMode m) {
  switch (m) {
    case ColorMode::kLeader: return Tr(lang, "mode.leader");
    case ColorMode::kPr: return Tr(lang, "mode.pr");
    case ColorMode::kMargin: return Tr(lang, "mode.margin");
    case ColorMode::kTurnout: return Tr(lang, "mode.turnout");
    case ColorMode::kReview: return Tr(lang, "mode.review");
    default: return "";
  }
}

uint32_t MixColor(uint32_t a, uint32_t b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  auto ch = [&](int shift) {
    const float x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
    return static_cast<uint32_t>(std::lround(x)) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

float Luminance(uint32_t c) {
  return (0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF)) / 255.f;
}

DashboardLayout Dashboard::Layout(int width, int height) {
  DashboardLayout l;
  l.scale = std::clamp(std::min(width / 1600.f, height / 900.f), 0.7f, 2.5f);
  l.panel_x = width - (430 + 16) * l.scale;
  return l;
}

uint32_t Dashboard::RegionColor(const DashboardModel& m, int region, float* strength) {
  *strength = 0;
  if (region < 0 || !m.results || !m.tree || region >= m.tree->size()) return kNeutral;
  const geo::Region& r = m.tree->region(region);
  if (r.level == geo::Level::kNation) return kNeutral;

  if (m.mode == ColorMode::kReview) {
    const auto& refs = m.data->info().referendums;
    if (refs.empty()) return kNeutral;
    const int idx = std::clamp(m.review, 0, static_cast<int>(refs.size()) - 1);
    const RefTally& t = m.results->ReferendumTally(refs[idx].id, region);
    if (t.agree + t.disagree <= 0) return kNeutral;
    const double share = t.AgreeShare();
    *strength = static_cast<float>(
        std::clamp(0.2 + 0.8 * (share - kDismissLo) / (kDismissHi - kDismissLo), 0.2, 1.0));
    if (t.Passes()) *strength = 1;
    return kDismiss;
  }

  if (m.mode == ColorMode::kPr) {
    const Race* pr = m.results->PrRaceForRegion(region);
    if (!pr) return kNeutral;
    const Tally& t = m.results->RaceTally(*pr, region);
    if (!HasVotes(t)) return kNeutral;
    const int lead = t.Leader();
    *strength = std::clamp(0.45f + static_cast<float>(MarginShare(t)) * 2.5f, 0.45f, 1.f) *
                ProgressFactor(t.Progress());
    return m.data->party(pr->candidates[lead].party).color;
  }

  if (r.level == geo::Level::kCounty) {
    const PrefSummary s = SummarizePref(m, r);
    switch (m.mode) {
      case ColorMode::kMargin:
        if (!s.with_votes) return kNeutral;
        *strength = 0.95f * ProgressFactor(s.progress);
        return MarginColor(s.margin);
      case ColorMode::kTurnout: {
        if (s.eligible <= 0 || s.ballots <= 0) return kNeutral;
        const double turnout = static_cast<double>(s.ballots) / static_cast<double>(s.eligible);
        *strength = static_cast<float>(
            std::clamp((turnout - kTurnoutLo) / (kTurnoutHi - kTurnoutLo), 0.05, 1.0));
        return kTurnoutColor;
      }
      default: {
        if (s.parties.empty() || s.with_leader == 0) return kNeutral;
        const float dominance = static_cast<float>(s.parties[0].second) / s.with_leader;
        *strength = (0.35f + 0.65f * dominance) * ProgressFactor(s.progress);
        return m.data->party(s.parties[0].first).color;
      }
    }
  }

  // District or counting unit: its SMD race, scoped to the region.
  const Race* race = m.results->SmdRaceForRegion(region);
  if (!race) return kNeutral;
  const Tally* t = nullptr;
  const int lead = SmdLeader(m, *race, region, &t);
  switch (m.mode) {
    case ColorMode::kMargin:
      if (!HasVotes(*t)) return kNeutral;
      *strength = 0.95f * ProgressFactor(t->Progress());
      return MarginColor(MarginShare(*t));
    case ColorMode::kTurnout:
      if (t->eligible <= 0 || t->ballots_cast <= 0) return kNeutral;
      *strength = static_cast<float>(
          std::clamp((t->Turnout() - kTurnoutLo) / (kTurnoutHi - kTurnoutLo), 0.05, 1.0));
      return kTurnoutColor;
    default:
      if (lead < 0) return kNeutral;
      if (!HasVotes(*t)) {
        *strength = 0.55f;  // called before any votes (ゼロ打ち)
      } else {
        *strength = std::clamp(0.45f + static_cast<float>(MarginShare(*t)) * 2.5f, 0.45f, 1.f) *
                    ProgressFactor(t->Progress());
      }
      return m.data->party(race->candidates[lead].party).color;
  }
}

void Dashboard::Render(const DashboardModel& m, uint8_t* pixels) {
  const SkImageInfo info = SkImageInfo::MakeN32Premul(m.width, m.height);
  std::unique_ptr<SkCanvas> canvas =
      SkCanvas::MakeRasterDirect(info, pixels, static_cast<size_t>(m.width) * 4);
  SkCanvas* c = canvas.get();
  c->clear(SK_ColorTRANSPARENT);
  L_ = Localizer(m.lang);
  // Japanese glyph forms for Japanese and English (Japanese names appear as
  // secondary text); Traditional Chinese forms for zh-TW.
  fonts_->SetJapanese(m.lang != Lang::kZhTW);
  const DashboardLayout layout = Layout(m.width, m.height);
  s_ = layout.scale;

  UpdateLive(m);
  hits_.clear();
  DrawInsets(c, m);
  DrawRings(c, m);
  DrawLabels(c, m);
  DrawCallouts(c, m);
  DrawChartView(c, m);  // secondary views slide over the map
  DrawHeader(c, m);
  DrawLeftColumn(c, m);
  const float margin = 16 * s_;
  // Leaves two lines under the panel for the map / photo credits.
  const SkRect panel = SkRect::MakeLTRB(layout.panel_x, margin, m.width - margin,
                                        m.height - margin - 44 * s_);
  const geo::Level level = m.tree->region(m.focus).level;
  const Race* pr = level != geo::Level::kNation ? m.results->PrRaceForRegion(m.focus) : nullptr;
  if (m.mode == ColorMode::kReview && !m.data->info().referendums.empty()) {
    DrawReviewPanel(c, m, panel);
  } else if (m.mode == ColorMode::kPr && pr) {
    DrawBlocPanel(c, m, panel, *pr);
  } else if (level == geo::Level::kNation) {
    DrawNationPanel(c, m, panel);
  } else if (level == geo::Level::kCounty) {
    DrawPrefecturePanel(c, m, panel);
  } else {
    DrawRacePanel(c, m, panel);
  }
  DrawTimeline(c, m);
  DrawChartTabs(c, m);
  DrawPip(c, m);
  DrawFooter(c, m);
  DrawToasts(c, m);
  DrawFloaters(c);
  DrawNotice(c, m);
  if (chart_t_ < 0.3f) DrawTooltip(c, m);
  if (m.show_help) DrawHelp(c, m);
}

void Dashboard::DrawHeader(SkCanvas* c, const DashboardModel& m) {
  const float x = 18 * s_;
  float y = 44 * s_;
  const auto& info = m.data->info();
  const float panel_x = Layout(m.width, m.height).panel_x;

  // Status chips: mode badge (simulation / replay) + count status with clock.
  const auto& snap = m.results->snapshot();
  struct ChipSpec {
    std::string text;
    uint32_t color;
  };
  std::vector<ChipSpec> chips;
  if (m.replay || snap.replay) {
    chips.push_back({L_.T("status.replay"), 0xFF8E44AD});
  } else if (m.simulated || snap.simulated) {
    chips.push_back({L_.T("status.simulation"), 0xFFD64545});
  }
  const bool sim_clock = m.simulated || snap.simulated;
  std::string clock;
  if (sim_clock && m.clock_minutes >= 0) {
    clock = election::SimulatedResultsSource::ClockLabel(m.clock_minutes);
  } else {
    clock = election::TimeLabel(snap.updated_at);
  }
  if (snap.status == ResultsStatus::kPreElection) {
    std::string status = L_.T("status.preelection");
    if (!sim_clock) {
      const Countdown cd = ComputeCountdown(info, m.now);
      switch (cd.phase) {
        case Countdown::Phase::kBefore:
          status += " · " + Fmt(L_.T("status.countdown"), {std::to_string(cd.days)});
          break;
        case Countdown::Phase::kElectionMorning:
          status += " · " + Fmt(L_.T("dash.countdown_open"), {HoursMinutes(cd.seconds)});
          break;
        case Countdown::Phase::kVoting:
          status = Fmt(L_.T("status.voting"), {info.polls_open, info.polls_close}) + " · " +
                   Fmt(L_.T("status.countdown_hours"), {HoursMinutes(cd.seconds)});
          break;
        case Countdown::Phase::kClosed:
          status = L_.T("status.awaiting");
          break;
      }
      chips.push_back({status, 0xFF3A6EA5});
      chips.push_back({Fmt(L_.T("dash.now_jst"), {cd.now_label}), 0xFF26415E});
    } else {
      if (!clock.empty()) status += " · " + clock;
      chips.push_back({status, 0xFF3A6EA5});
    }
  } else if (snap.status == ResultsStatus::kCounting) {
    std::string status = L_.T("status.counting");
    if (!clock.empty()) status += " · " + clock;
    chips.push_back({status, kLeadAmber});
  } else {
    std::string status = L_.T("status.final");
    if (sim_clock && !clock.empty()) status += " · " + clock;
    chips.push_back({status, kWonGreen});
  }
  const float chip_h = 26 * s_;
  float chips_w = 0;
  for (const ChipSpec& ch : chips) chips_w += ChipWidth(fonts_, ch.text, chip_h) + 8 * s_;

  std::string title = L_.ElectionName(info);
  if (title.empty()) title = L_.T("app.title");
  const SkFont title_font = fonts_->Bold((m.lang == Lang::kEn ? 24 : 28) * s_);
  const float title_max = std::max(200 * s_, panel_x - x - chips_w - 30 * s_);
  title = Ellipsize(title_font, title, title_max);
  DrawText(c, title, x, y, title_font, kText);
  float cx = x + TextWidth(title_font, title) + 12 * s_;
  for (const ChipSpec& ch : chips) {
    cx += Chip(c, fonts_, ch.text, cx, y - 20 * s_, chip_h, ch.color, s_) + 8 * s_;
  }

  y += 24 * s_;
  std::string sub = Fmt(L_.T("header.subtitle"),
                        {FormatDate(m.lang, info.date), info.polls_open, info.polls_close});
  if (!info.dissolution.empty()) {
    sub += " · " + Fmt(L_.T("header.dissolution"), {FormatDate(m.lang, info.dissolution)});
  }
  DrawText(c, Ellipsize(fonts_->Regular(14 * s_), sub, panel_x - x - 20 * s_), x, y,
           fonts_->Regular(14 * s_), kText2);

  // Breadcrumb.
  y += 30 * s_;
  const std::vector<int> path = m.tree->Path(m.focus);
  float bx = x;
  for (size_t i = 0; i < path.size(); ++i) {
    const bool last = i + 1 == path.size();
    const SkFont f = last ? fonts_->Bold(19 * s_) : fonts_->Regular(19 * s_);
    bx += DrawText(c, L_.RegionName(m.tree->region(path[i])), bx, y, f, last ? kAccent : kText2);
    if (path[i] == m.pinned) bx += DrawText(c, " ★", bx, y, fonts_->Regular(15 * s_), kAccent);
    if (!last) bx += DrawText(c, "  ›  ", bx, y, fonts_->Regular(19 * s_), kText3);
  }
  const geo::Region& focus = m.tree->region(m.focus);
  std::string alt;
  if (focus.level != geo::Level::kNation) {
    alt = m.lang == Lang::kEn ? focus.name_ja : focus.name_en;
    if (m.lang == Lang::kZhTW && focus.name_zh != focus.name_ja && !focus.name_ja.empty()) {
      alt = focus.name_ja + " · " + focus.name_en;
    }
  }
  if (!alt.empty()) DrawText(c, alt, x, y + 20 * s_, fonts_->Regular(12.5f * s_), kText3);
}

void Dashboard::DrawLeftColumn(SkCanvas* c, const DashboardModel& m) {
  const float x = 16 * s_;
  const float w = 330 * s_;
  float y = 150 * s_;
  if (m.show_news) {
    const float bottom = m.height - 140 * s_;
    const float news_h = feed_.empty() ? bottom - y : (bottom - y) * 0.68f;
    DrawNewsPanel(c, m, x, y, w, news_h);
    DrawFeed(c, m, x, y + news_h + 10 * s_, w, bottom - y - news_h - 10 * s_);
    return;
  }
  const float pad = 14 * s_;
  const geo::Region& focus = m.tree->region(m.focus);
  const auto& info = m.data->info();

  if (focus.level == geo::Level::kNation) {
    // The two tiers on the ballot.
    const float row = 21 * s_;
    const float h = pad * 2 + 30 * s_ + row * info.offices.size() + 44 * s_;
    Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
    float ty = y + pad + 16 * s_;
    DrawText(c, L_.T("dash.offices.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
    ty += 22 * s_;
    DrawText(c, L_.T("dash.offices.seats"), x + w - pad - 80 * s_, ty - 2 * s_,
             fonts_->Regular(11 * s_), kText3, Align::kRight);
    DrawText(c, L_.T("dash.offices.candidates"), x + w - pad, ty - 2 * s_,
             fonts_->Regular(11 * s_), kText3, Align::kRight);
    for (const auto& o : info.offices) {
      ty += row;
      DrawText(c, Ellipsize(fonts_->Bold(13.5f * s_), L_.OfficeName(o), w - 150 * s_), x + pad,
               ty, fonts_->Bold(13.5f * s_), kText2);
      DrawText(c, FormatThousands(o.seats), x + w - pad - 80 * s_, ty, fonts_->Regular(13.5f * s_),
               kText, Align::kRight);
      DrawText(c, FormatThousands(o.candidates), x + w - pad, ty, fonts_->Regular(13.5f * s_),
               kText2, Align::kRight);
    }
    ty += row + 2 * s_;
    DrawText(c,
             Ellipsize(fonts_->Regular(12 * s_),
                       Fmt(L_.T("dash.offices.total"), {FormatThousands(info.total_seats),
                                                        FormatThousands(info.total_candidates)}),
                       w - 2 * pad),
             x + pad, ty, fonts_->Regular(12 * s_), kText3);
    ty += 18 * s_;
    DrawText(c,
             Fmt(L_.T("seats.majority"), {std::to_string(info.majority)}) + " · " +
                 Fmt(L_.T("seats.supermajority"), {std::to_string(info.supermajority)}),
             x + pad, ty, fonts_->Regular(12 * s_), kText3);
    y += h + 12 * s_;
  } else {
    // Area facts; for districts the 2024 winner.
    const int pref_id = m.tree->AncestorAt(m.focus, geo::Level::kCounty);
    const Race* race = m.results->SmdRaceForRegion(m.focus);
    const election::Bloc* bloc = m.data->BlocForPref(focus.county_code);
    const float h = (race ? 124 : 96) * s_ + (race && !race->incumbent.note.empty() ? 16 * s_ : 0);
    Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
    float ty = y + pad + 16 * s_;
    DrawText(c, L_.T("area.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
    ty += 24 * s_;
    std::string facts = Fmt(L_.T("area.size"), {Fixed(focus.area_km2, 1)});
    if (!focus.children.empty()) {
      facts += " · " + Fmt(L_.T(focus.level == geo::Level::kCounty ? "area.districts" : "area.units"),
                           {std::to_string(focus.children.size())});
    }
    DrawText(c, Ellipsize(fonts_->Regular(13.5f * s_), facts, w - 2 * pad), x + pad, ty,
             fonts_->Regular(13.5f * s_), kText2);
    ty += 22 * s_;
    std::string where;
    if (focus.level != geo::Level::kCounty && pref_id >= 0) {
      where = L_.RegionName(m.tree->region(pref_id));
    }
    if (bloc) {
      where += (where.empty() ? "" : " · ") + Fmt(L_.T("dash.area.bloc"), {L_.BlocName(*bloc)});
    }
    DrawText(c, Ellipsize(fonts_->Regular(13 * s_), where, w - 2 * pad), x + pad, ty,
             fonts_->Regular(13 * s_), kText2);
    if (race) {
      ty += 28 * s_;
      const Party& p = m.data->party(race->incumbent.party);
      float cx = x + pad;
      cx += DrawText(c, std::string(L_.T("dash.area.winner2024")) + " ", cx, ty,
                     fonts_->Regular(13 * s_), kText2);
      std::string inc_name = race->incumbent.name_ja;
      for (const Candidate& cand : race->candidates) {
        if (cand.name_ja == race->incumbent.name_ja || cand.name_legal == race->incumbent.name_ja) {
          inc_name = L_.CandidateName(cand);
        }
      }
      if (inc_name.empty()) inc_name = "—";
      const SkFont nf = fonts_->Bold(15 * s_);
      inc_name = Ellipsize(nf, inc_name, w - 2 * pad - (cx - x - pad) - 120 * s_);
      cx += DrawText(c, inc_name, cx, ty, nf, kText) + 8 * s_;
      if (!race->incumbent.party.empty()) {
        cx += Chip(c, fonts_, L_.PartyShort(p), cx, ty - 15 * s_, 20 * s_, p.color, s_) + 6 * s_;
      }
      if (!race->incumbent.running && !race->incumbent.name_ja.empty()) {
        Chip(c, fonts_, L_.T("dash.area.not_running"), cx, ty - 15 * s_, 20 * s_, 0xFF9FB0C3, s_,
             true);
      }
      if (!race->incumbent.note.empty()) {
        ty += 20 * s_;
        DrawText(c, Ellipsize(fonts_->Regular(12 * s_), race->incumbent.note, w - 2 * pad), x + pad,
                 ty, fonts_->Regular(12 * s_), kText3);
      }
    }
    y += h + 12 * s_;
  }

  // National review card (the full panel replaces it in review mode).
  if (!info.referendums.empty() && m.mode != ColorMode::kReview) {
    y += DrawReviewCard(c, m, x, y, w) + 12 * s_;
  }
  // Live event feed fills the rest of the column (above the timeline).
  DrawFeed(c, m, x, y, w, m.height - 140 * s_ - y);
}

float Dashboard::DrawReviewCard(SkCanvas* c, const DashboardModel& m, float x, float y, float w) {
  const auto& refs = m.data->info().referendums;
  const float pad = 14 * s_;
  const float row = 38 * s_;
  const float h = pad * 2 + 22 * s_ + row * refs.size() + 16 * s_;
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
  float ty = y + pad + 15 * s_;
  const int where = m.focus;
  DrawText(c, Ellipsize(fonts_->Bold(15 * s_), L_.T("review.title"), w - 2 * pad - 90 * s_),
           x + pad, ty, fonts_->Bold(15 * s_), kText);
  const geo::Region& focus = m.tree->region(m.focus);
  const int pref = focus.level == geo::Level::kNation
                       ? -1
                       : m.tree->AncestorAt(m.focus, geo::Level::kCounty);
  DrawText(c,
           Ellipsize(fonts_->Regular(11.5f * s_),
                     pref >= 0 ? L_.RegionName(m.tree->region(pref)) : L_.T("level.nation"),
                     86 * s_),
           x + w - pad, ty, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  ty += 8 * s_;
  for (size_t i = 0; i < refs.size(); ++i) {
    const auto& ref = refs[i];
    const RefTally& t = m.results->ReferendumTally(ref.id, pref >= 0 ? pref : where);
    const bool has = t.agree + t.disagree > 0;
    ty += 18 * s_;
    const bool selected = static_cast<int>(i) == m.review;
    DrawText(c, Ellipsize(fonts_->Bold(13.5f * s_), L_.Justice(ref), w * 0.5f), x + pad, ty,
             fonts_->Bold(13.5f * s_), selected ? kText : kText2);
    std::string right = has ? Fmt(L_.T("review.dismiss_share"), {Percent(t.AgreeShare())}) : "—";
    float rx = x + w - pad;
    if (has && t.Progress() >= 1) {
      const bool dismissed = t.Passes();
      const std::string verdict = L_.T(dismissed ? "review.dismissed" : "review.retained");
      const float cw = ChipWidth(fonts_, verdict, 17 * s_);
      Chip(c, fonts_, verdict, rx - cw, ty - 13 * s_, 17 * s_, dismissed ? kDismiss : kRetain, s_);
      rx -= cw + 6 * s_;
    }
    DrawText(c, right, rx, ty, fonts_->Bold(13 * s_), has ? SkColor(kDismiss) : kText3,
             Align::kRight);
    ty += 8 * s_;
    // Full 0-100% bar with the 50% dismissal line.
    const SkRect bar = SkRect::MakeXYWH(x + pad, ty, w - 2 * pad, 6 * s_);
    Bar(c, bar, has ? static_cast<float>(t.AgreeShare()) : 0.f, kDismiss);
    SkPaint mid = Fill(SkColorSetARGB(200, 255, 255, 255));
    mid.setStrokeWidth(1.5f * s_);
    c->drawLine(bar.centerX(), bar.fTop - 3 * s_, bar.centerX(), bar.fBottom + 3 * s_, mid);
    ty += 12 * s_;
  }
  ty += 14 * s_;
  std::string rule = L_.T("review.rule");
  if (pref >= 0) rule += " · " + std::string(L_.T("review.prefecture_level"));
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), rule, w - 2 * pad), x + pad, ty,
           fonts_->Regular(11.5f * s_), kText3);
  return h;
}

void Dashboard::DrawNationPanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 20 * s_;
  const auto& info = m.data->info();
  const int total_seats = info.total_seats > 0 ? info.total_seats : 465;
  const election::SeatSummary* seats = m.seats;
  const bool any = seats && (seats->smd_reporting > 0 || seats->pr_votes > 0 || seats->smd_decided > 0);
  const bool official = OfficialFinal(m);

  DrawText(c, L_.T("dash.nation.title"), x, y, fonts_->Bold(22 * s_), kText);
  DrawText(c, Fmt(L_.T("seats.total"), {std::to_string(total_seats)}), x + w, y,
           fonts_->Bold(15 * s_), kAccent, Align::kRight);
  y += 22 * s_;
  int decided = 0, leading_total = 0;
  if (seats) {
    for (const auto& p : seats->parties) {
      decided += p.decided();
      leading_total += p.total();
    }
  }
  std::string status;
  if (!any) {
    status = L_.T("dash.nation.before");
  } else if (official || decided >= total_seats) {
    status = L_.T("dash.nation.final");
  } else {
    status = Fmt(L_.T("seats.split"),
                 {std::to_string(decided), std::to_string(std::max(0, leading_total - decided))});
  }
  DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), status, w), x, y, fonts_->Regular(12.5f * s_),
           kText2);
  y += 12 * s_;

  // Seat bar: decided seats solid, leading/allocated seats lighter.
  const float bar_h = 18 * s_;
  const SkRect seat_bar = SkRect::MakeXYWH(x, y, w, bar_h);
  c->drawRRect(SkRRect::MakeRectXY(seat_bar, 4 * s_, 4 * s_), Fill(kTrack));
  if (any) {
    c->save();
    c->clipRRect(SkRRect::MakeRectXY(seat_bar, 4 * s_, 4 * s_), true);
    const float unit = w / total_seats;
    float sx = x;
    for (const auto& p : seats->parties) {
      const uint32_t col = m.data->party(p.party).color;
      const float dw = unit * p.decided();
      const float lw = unit * (p.total() - p.decided());
      const float shown_d = Approach(anim_, "seatbar:d:" + p.party, dw, 5.f);
      const float shown_l = Approach(anim_, "seatbar:l:" + p.party, lw, 5.f);
      c->drawRect(SkRect::MakeXYWH(sx, y, shown_d, bar_h), Fill(col));
      sx += shown_d;
      c->drawRect(SkRect::MakeXYWH(sx, y, shown_l, bar_h), Fill(SkColorSetA(col, 120)));
      sx += shown_l;
      if (shown_d + shown_l > 1.5f) {
        c->drawRect(SkRect::MakeXYWH(sx - 0.5f, y, 1, bar_h), Fill(SkColorSetARGB(90, 0, 0, 0)));
      }
    }
    c->restore();
  }
  auto threshold = [&](int seats_needed, bool label_left, const std::string& label) {
    const float lx = x + w * seats_needed / static_cast<float>(total_seats);
    SkPaint line = Fill(SK_ColorWHITE);
    line.setStrokeWidth(1.5f * s_);
    c->drawLine(lx, y - 3 * s_, lx, y + bar_h + 3 * s_, line);
    DrawText(c, label, label_left ? lx - 3 * s_ : lx + 3 * s_, y + bar_h + 14 * s_,
             fonts_->Regular(11 * s_), kText2, label_left ? Align::kRight : Align::kLeft);
  };
  if (info.majority > 0) {
    threshold(info.majority, true, Fmt(L_.T("seats.majority"), {std::to_string(info.majority)}));
  }
  if (info.supermajority > 0) {
    threshold(info.supermajority, false,
              Fmt(L_.T("seats.supermajority"), {std::to_string(info.supermajority)}));
  }
  y += bar_h + 36 * s_;

  if (!any) {
    // Before counting: candidates by party.
    struct Row {
      std::string party;
      int smd = 0, pr = 0;
    };
    std::map<std::string, Row> rows;
    for (const Race& r : m.data->races()) {
      for (const Candidate& cand : r.candidates) {
        Row& row = rows[cand.party];
        row.party = cand.party;
        if (r.is_smd()) ++row.smd;
        else row.pr += static_cast<int>(cand.list.size());
      }
    }
    std::vector<Row> ordered;
    for (auto& [code, row] : rows) ordered.push_back(row);
    std::sort(ordered.begin(), ordered.end(),
              [](const Row& a, const Row& b) { return a.smd + a.pr > b.smd + b.pr; });
    DrawText(c, L_.T("dash.nation.cands_title"), x, y, fonts_->Bold(15 * s_), kText);
    y += 20 * s_;
    const float c1 = x + w - 70 * s_, c2 = x + w;
    DrawText(c, L_.T("seats.smd"), c1, y, fonts_->Regular(11 * s_), kText3, Align::kRight);
    DrawText(c, L_.T("dash.col.pr_list"), c2, y, fonts_->Regular(11 * s_), kText3, Align::kRight);
    const float row_h = 22 * s_;
    for (const Row& row : ordered) {
      if (y + row_h > panel.fBottom - pad) break;
      y += row_h;
      const Party& p = m.data->party(row.party);
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y - 11 * s_, 12 * s_, 12 * s_), 3 * s_,
                                       3 * s_),
                   Fill(p.color));
      DrawText(c, Ellipsize(fonts_->Bold(13.5f * s_), L_.PartyName(p), c1 - x - 80 * s_),
               x + 20 * s_, y, fonts_->Bold(13.5f * s_), kText);
      DrawText(c, std::to_string(row.smd), c1, y, fonts_->Regular(13.5f * s_), kText,
               Align::kRight);
      DrawText(c, std::to_string(row.pr), c2, y, fonts_->Regular(13.5f * s_), kText2,
               Align::kRight);
    }
    return;
  }

  // Leading party vs the majority lines.
  const election::PartySeats& top = seats->parties.front();
  const std::string top_name = L_.PartyShort(m.data->party(top.party));
  std::string verdict;
  if (info.supermajority > 0 && top.total() >= info.supermajority) {
    verdict = Fmt(L_.T("dash.nation.supermajority_reached"), {top_name, std::to_string(top.total())});
  } else if (info.majority > 0 && top.total() >= info.majority) {
    verdict = Fmt(L_.T("dash.nation.majority_reached"), {top_name, std::to_string(top.total())});
  } else {
    verdict = std::string(L_.T("dash.nation.no_majority")) + " · " +
              Fmt(L_.T("dash.nation.to_majority"),
                  {top_name, std::to_string(std::max(0, info.majority - top.total()))});
  }
  DrawText(c, Ellipsize(fonts_->Bold(14 * s_), verdict, w), x, y, fonts_->Bold(14 * s_), kAccent);
  y += 26 * s_;

  // Party table: SMD + PR = total (decided), PR vote share.
  const float c_smd = x + 196 * s_, c_pr = x + 240 * s_, c_tot = x + 290 * s_,
              c_dec = x + 334 * s_, c_share = x + w;
  const SkFont hf = fonts_->Regular(11 * s_);
  DrawText(c, L_.T("dash.col.party"), x, y, hf, kText3);
  DrawText(c, L_.T("seats.smd"), c_smd, y, hf, kText3, Align::kRight);
  DrawText(c, L_.T("seats.pr"), c_pr, y, hf, kText3, Align::kRight);
  DrawText(c, L_.T("seats.sum"), c_tot, y, hf, kText3, Align::kRight);
  DrawText(c, L_.T("seats.decided"), c_dec, y, hf, kText3, Align::kRight);
  DrawText(c, L_.T("dash.col.pr_share"), c_share, y, hf, kText3, Align::kRight);
  y += 6 * s_;
  const float row_h = 23 * s_;
  const float close_min = 120 * s_;  // keep room for the closest districts
  for (const auto& ps : seats->parties) {
    if (ps.total() == 0 && ps.pr_votes == 0) continue;
    if (y + row_h > panel.fBottom - pad - close_min) break;
    y += row_h;
    const Party& p = m.data->party(ps.party);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y - 11 * s_, 12 * s_, 12 * s_), 3 * s_,
                                     3 * s_),
                 Fill(p.color));
    DrawText(c, Ellipsize(fonts_->Bold(13.5f * s_), L_.PartyShort(p), c_smd - x - 56 * s_),
             x + 18 * s_, y, fonts_->Bold(13.5f * s_), kText);
    const SkFont nf = fonts_->Regular(13.5f * s_);
    DrawText(c, std::to_string(ps.smd), c_smd, y, nf, kText2, Align::kRight);
    DrawText(c, std::to_string(ps.pr), c_pr, y, nf, kText2, Align::kRight);
    const double shown = Roll("seats:" + ps.party, ps.total());
    DrawText(c, std::to_string(static_cast<int>(std::lround(shown))), c_tot, y,
             fonts_->Bold(15 * s_), kText, Align::kRight);
    DrawText(c, std::to_string(ps.decided()), c_dec, y, fonts_->Regular(12 * s_),
             ps.decided() == ps.total() ? kText3 : SkColor(kAccent), Align::kRight);
    const double share =
        seats->pr_votes > 0 ? static_cast<double>(ps.pr_votes) / seats->pr_votes : 0.0;
    DrawText(c, ps.pr_votes > 0 ? Percent(share) : "—", c_share, y, fonts_->Regular(12.5f * s_),
             kText2, Align::kRight);
  }
  y += 12 * s_;
  Separator(c, x, y, w);
  y += 18 * s_;
  const int smd_total = static_cast<int>(seats->smd.size());
  const int pr_total = static_cast<int>(seats->pr.size());
  DrawText(c,
           Fmt(L_.T("dash.nation.smd_progress"),
               {std::to_string(seats->smd_decided), std::to_string(smd_total)}) +
               "   " +
               Fmt(L_.T("dash.nation.pr_progress"),
                   {std::to_string(seats->pr_final), std::to_string(pr_total)}),
           x, y, fonts_->Regular(12 * s_), kText2);
  y += 14 * s_;

  // Closest districts (click to open).
  std::vector<const election::SmdOutcome*> close;
  for (const auto& o : seats->smd) {
    if (o.valid > 0 && o.runner_up >= 0 && o.leader >= 0) close.push_back(&o);
  }
  std::sort(close.begin(), close.end(), [](const auto* a, const auto* b) {
    const double ma = static_cast<double>(a->margin) / std::max<int64_t>(1, a->valid);
    const double mb = static_cast<double>(b->margin) / std::max<int64_t>(1, b->valid);
    return ma < mb;
  });
  if (close.empty() || y + 50 * s_ > panel.fBottom - pad) return;
  y += 16 * s_;
  DrawText(c, L_.T("dash.nation.close"), x, y, fonts_->Bold(14 * s_), kText);
  y += 4 * s_;
  const float crow = 22 * s_;
  const int hover_district = m.hover >= 0 ? m.tree->AncestorAt(m.hover, geo::Level::kTown) : -1;
  for (const auto* o : close) {
    if (y + crow > panel.fBottom - pad) break;
    const Race& race = *o->race;
    const SkRect row = SkRect::MakeXYWH(x - 6 * s_, y + 2 * s_, w + 12 * s_, crow);
    const int district = m.tree->FindByCode(race.region);
    if (district >= 0 && district == hover_district) {
      c->drawRRect(SkRRect::MakeRectXY(row, 6 * s_, 6 * s_), Fill(SkColorSetARGB(40, 255, 255, 255)));
    }
    if (auto it = flash_.find("row:" + race.id); it != flash_.end() && it->second > 0) {
      c->drawRRect(SkRRect::MakeRectXY(row, 6 * s_, 6 * s_),
                   Fill(SkColorSetARGB(static_cast<U8CPU>(90 * it->second), 245, 197, 66)));
    }
    AddHit(row, {DashboardHit::Kind::kRace, ChartKind::kMap, &race});
    y += crow;
    const float ty = y - 4 * s_;
    const Candidate& lead = race.candidates[o->leader];
    const Candidate& second = race.candidates[o->runner_up];
    const Party& lp = m.data->party(lead.party);
    const Party& sp = m.data->party(second.party);
    DrawText(c, Ellipsize(fonts_->Bold(13 * s_), L_.RaceTitle(race), 92 * s_), x, ty,
             fonts_->Bold(13 * s_), kText);
    float cx = x + 96 * s_;
    c->drawCircle(cx + 4 * s_, ty - 4.5f * s_, 4 * s_, Fill(lp.color));
    cx += 11 * s_;
    cx += DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), L_.CandidateName(lead), 82 * s_), cx,
                   ty, fonts_->Regular(12.5f * s_), kText);
    cx = x + 204 * s_;
    DrawText(c, "vs", cx, ty, fonts_->Regular(11 * s_), kText3);
    cx += 16 * s_;
    c->drawCircle(cx + 4 * s_, ty - 4.5f * s_, 4 * s_, Fill(sp.color));
    cx += 11 * s_;
    DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), L_.CandidateName(second), x + w - cx - 64 * s_),
             cx, ty, fonts_->Regular(12.5f * s_), kText2);
    DrawText(c, Fmt(L_.T("dash.margin_votes"), {FormatThousands(o->margin)}), x + w, ty,
             fonts_->Bold(12 * s_), o->decided ? kText2 : SkColor(kAccent), Align::kRight);
  }
}

void Dashboard::DrawPrefecturePanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const geo::Region& focus = m.tree->region(m.focus);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  const bool official = OfficialFinal(m);

  struct Row {
    int district = -1;
    const Race* race = nullptr;
    const Tally* t = nullptr;
    int lead = -1;
  };
  std::vector<Row> rows;
  for (int d : focus.children) {
    Row row;
    row.district = d;
    row.race = m.results->SmdRaceForRegion(d);
    if (!row.race) continue;
    row.lead = SmdLeader(m, *row.race, d, &row.t);
    rows.push_back(row);
  }

  DrawText(c, Ellipsize(fonts_->Bold(24 * s_), L_.RegionName(focus), w - 110 * s_), x, y,
           fonts_->Bold(24 * s_), kText);
  DrawText(c, Fmt(L_.T("area.districts"), {std::to_string(rows.size())}), x + w, y,
           fonts_->Regular(13 * s_), kText2, Align::kRight);
  y += 24 * s_;
  // Districts led / won per party.
  const PrefSummary sum = SummarizePref(m, focus);
  if (sum.parties.empty()) {
    DrawText(c, L_.T("dash.race.not_started"), x, y, fonts_->Regular(13 * s_), kText2);
  } else {
    float cx = x;
    for (const auto& [party, n] : sum.parties) {
      const Party& p = m.data->party(party);
      const std::string text = L_.PartyShort(p) + " " + std::to_string(n);
      const float cw = ChipWidth(fonts_, text, 20 * s_);
      if (cx + cw > x + w) break;
      cx += Chip(c, fonts_, text, cx, y - 15 * s_, 20 * s_, p.color, s_) + 6 * s_;
    }
  }
  y += 14 * s_;

  // PR bloc summary at the bottom.
  const Race* pr = m.results->PrRaceForRegion(m.focus);
  const float bloc_h = pr ? 78 * s_ : 0;
  const float list_bottom = panel.fBottom - pad - bloc_h;

  const float row_h = std::clamp((list_bottom - y) / std::max<size_t>(1, rows.size()), 19 * s_,
                                 46 * s_);
  const float avatar = std::min(row_h - 4 * s_, 28 * s_);
  const SkFont name_font = fonts_->Bold(std::min(13.5f * s_, row_h * 0.62f));
  float name_w = 0;
  for (const Row& row : rows) name_w = std::max(name_w, TextWidth(name_font, L_.RaceTitle(*row.race)));
  name_w = std::min(name_w + 8 * s_, 108 * s_);
  const int hover_district = m.hover >= 0 ? m.tree->AncestorAt(m.hover, geo::Level::kTown) : -1;
  for (const Row& row : rows) {
    if (y + row_h > list_bottom + 1) break;
    const Race& race = *row.race;
    const float mid_y = y + row_h / 2;
    const SkRect rect = SkRect::MakeXYWH(x - 6 * s_, y, w + 12 * s_, row_h);
    if (row.district == hover_district) {
      c->drawRRect(SkRRect::MakeRectXY(rect, 6 * s_, 6 * s_), Fill(SkColorSetARGB(40, 255, 255, 255)));
    }
    if (auto it = flash_.find("row:" + race.id); it != flash_.end() && it->second > 0) {
      c->drawRRect(SkRRect::MakeRectXY(rect, 6 * s_, 6 * s_),
                   Fill(SkColorSetARGB(static_cast<U8CPU>(90 * it->second), 245, 197, 66)));
    }
    AddHit(rect, {DashboardHit::Kind::kRace, ChartKind::kMap, &race});
    const float text_y = mid_y + name_font.getSize() * 0.36f;
    DrawText(c, Ellipsize(name_font, L_.RaceTitle(race), name_w - 6 * s_), x, text_y, name_font,
             kText);
    const float list_x = x + name_w;
    const Tally& t = *row.t;
    if (row.lead < 0) {
      // Before counting: every candidate's avatar.
      float ax = list_x;
      const float step = std::min(avatar + 3 * s_, (x + w - list_x - 40 * s_) /
                                                       std::max<size_t>(1, race.candidates.size()));
      for (const Candidate& cand : race.candidates) {
        avatars_->Draw(c, cand, m.data->party(cand.party), ax, mid_y - avatar / 2, avatar);
        ax += step;
      }
      DrawText(c, Fmt(L_.T("dash.cands_n"), {std::to_string(race.candidates.size())}), x + w,
               text_y, fonts_->Regular(12 * s_), kText3, Align::kRight);
    } else {
      const Candidate& cand = race.candidates[row.lead];
      const Party& p = m.data->party(cand.party);
      c->drawRect(SkRect::MakeXYWH(x - 6 * s_, mid_y - row_h * 0.35f, 3 * s_, row_h * 0.7f),
                  Fill(p.color));
      avatars_->Draw(c, cand, p, list_x, mid_y - avatar / 2, avatar);
      const float nx = list_x + avatar + 6 * s_;
      const SkFont cf = fonts_->Bold(std::min(13 * s_, row_h * 0.6f));
      DrawText(c, Ellipsize(cf, L_.CandidateName(cand), 84 * s_), nx, text_y, cf, kText);
      float bx = nx + 88 * s_;
      const float chip_h = std::min(16 * s_, row_h - 3 * s_);
      bx += Chip(c, fonts_, L_.PartyShort(p), bx, mid_y - chip_h / 2, chip_h, p.color, s_) + 6 * s_;
      // 当選 / 当確 mark.
      const bool won = Complete(t) || (official && HasVotes(t));
      const bool called = !won && CalledOf(m, race) == row.lead;
      float bar_right = x + w - 50 * s_;
      if (won || called) {
        const std::string mark = L_.T(won ? "stamp.elected" : "stamp.called");
        const float mw = ChipWidth(fonts_, mark, chip_h);
        Chip(c, fonts_, mark, bar_right - mw, mid_y - chip_h / 2, chip_h,
             won ? kWonGreen : kCalledRed, s_);
        bar_right -= mw + 6 * s_;
      }
      const float bw = bar_right - bx;
      if (bw > 20 * s_) {
        const double share =
            HasVotes(t) ? Roll("share:" + race.id, t.Share(row.lead) * 10000) / 10000 : 0.0;
        Bar(c, SkRect::MakeXYWH(bx, mid_y - 3 * s_, bw, 6 * s_), static_cast<float>(share),
            p.color);
        if (row_h > 24 * s_) {
          Bar(c, SkRect::MakeXYWH(bx, mid_y + 5 * s_, bw, 2 * s_),
              static_cast<float>(t.Progress()), kAccent, SkColorSetARGB(25, 255, 255, 255));
        }
      }
      DrawText(c, HasVotes(t) ? Percent(t.Share(row.lead)) : "—", x + w, text_y,
               fonts_->Bold(std::min(13 * s_, row_h * 0.6f)), kText, Align::kRight);
    }
    y += row_h;
  }

  if (!pr) return;
  // PR bloc: seats per list (press 2 for the full panel).
  float by = panel.fBottom - pad - bloc_h + 10 * s_;
  Separator(c, x, by, w);
  by += 20 * s_;
  const bool official_pr = OfficialFinal(m);
  DrawText(c, Ellipsize(fonts_->Bold(14 * s_),
                        Fmt(L_.T("dash.pref.bloc"), {L_.RaceTitle(*pr), std::to_string(pr->seats)}),
                        w - 110 * s_),
           x, by, fonts_->Bold(14 * s_), kText);
  DrawText(c, L_.T("dash.pref.bloc_hint"), x + w, by, fonts_->Regular(11.5f * s_), kText3,
           Align::kRight);
  by += 24 * s_;
  const Tally& pt = m.results->RaceTotal(*pr);
  std::vector<int> order = pt.Ranking();
  if (!HasVotes(pt)) {
    DrawText(c, L_.T("dash.race.not_started"), x, by - 4 * s_, fonts_->Regular(12.5f * s_), kText2);
    return;
  }
  float cx = x;
  for (int li : order) {
    const int n = ListSeats(m, *pr, li, official_pr);
    if (n <= 0) continue;
    const Party& p = m.data->party(pr->candidates[li].party);
    const std::string text = L_.PartyShort(p) + " " + std::to_string(n);
    const float cw = ChipWidth(fonts_, text, 20 * s_);
    if (cx + cw > x + w) break;
    cx += Chip(c, fonts_, text, cx, by - 15 * s_, 20 * s_, p.color, s_) + 6 * s_;
  }
}

void Dashboard::DrawRacePanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const geo::Region& focus = m.tree->region(m.focus);
  const Race* race = m.results->SmdRaceForRegion(m.focus);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  if (!race) {
    DrawText(c, L_.T("dash.race.none"), x, y, fonts_->Bold(18 * s_), kText);
    return;
  }
  const bool unit = focus.level == geo::Level::kVillage;
  const std::string level_chip = L_.T(unit ? "level.village" : "level.town");
  const float chip_w = ChipWidth(fonts_, level_chip, 20 * s_);
  const SkFont title_font = fonts_->Bold(m.lang == Lang::kEn ? 22 * s_ : 24 * s_);
  DrawText(c, Ellipsize(title_font, L_.RaceTitle(*race), w - chip_w - 8 * s_), x, y, title_font,
           kText);
  Chip(c, fonts_, level_chip, x + w - chip_w, y - 18 * s_, 20 * s_, 0xFF9FB0C3, s_, true);
  y += 22 * s_;
  std::string scope = unit ? Fmt(L_.T("dash.race.scope_unit"), {L_.RegionName(focus)})
                           : std::string(L_.T("dash.race.scope_district"));
  if (const election::Bloc* bloc = m.data->BlocById(race->bloc)) {
    scope += " · " + Fmt(L_.T("dash.area.bloc"), {L_.BlocName(*bloc)});
  }
  DrawText(c, Ellipsize(fonts_->Regular(14 * s_), scope, w), x, y, fonts_->Regular(14 * s_),
           kAccent);
  y += 18 * s_;

  const Tally& t = m.results->RaceTally(*race, m.focus);
  const Tally& total = m.results->RaceTotal(*race);
  const bool counting = HasVotes(t);
  const RaceStatus* status = StatusOf(m, *race);
  const bool official = OfficialFinal(m);
  const bool race_final = Complete(total) || (official && HasVotes(total));
  const int winner = race_final && HasVotes(total) ? total.Leader() : -1;
  const int race_leader = HasVotes(total) ? total.Leader() : -1;
  // Rolling (animated) vote counts for this view.
  const std::string key_base = race->id + ":" + std::to_string(m.focus) + ":";
  std::vector<double> shown(race->candidates.size(), 0);
  std::vector<double> added(race->candidates.size(), 0);
  double shown_total = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    const double target = i < t.votes.size() ? static_cast<double>(t.votes[i]) : 0;
    shown[i] = Roll(key_base + std::to_string(i), target, &added[i]);
    shown_total += shown[i];
  }
  // Progress + turnout.
  Bar(c, SkRect::MakeXYWH(x, y, w, 6 * s_), static_cast<float>(t.Progress()), kAccent);
  y += 22 * s_;
  const std::string progress =
      t.units_total > 0 ? Fmt(L_.T("dash.race.progress"), {std::to_string(t.units_counted),
                                                            std::to_string(t.units_total),
                                                            Percent(t.Progress(), 0)})
                        : std::string(L_.T("dash.race.not_started"));
  DrawText(c, progress, x, y, fonts_->Regular(13 * s_), kText2);
  if (t.ballots_cast > 0 && t.eligible > 0) {
    DrawText(c, std::string(L_.T("mode.turnout")) + " " + Percent(t.Turnout()), x + w, y,
             fonts_->Regular(13 * s_), kText2, Align::kRight);
  }
  y += 14 * s_;

  // Candidate cards: by votes once counting, else in filing order.
  std::vector<int> order(race->candidates.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  if (counting) order = t.Ranking();
  const float avail = panel.fBottom - pad - 40 * s_ - y;
  const float card_h = std::min(92 * s_, avail / std::max<size_t>(1, order.size()));
  const float photo = std::min(card_h - 16 * s_, 62 * s_);
  const float list_top = y;
  // Cards slide to their new rank (drawn back-to-front so movers stay on top).
  std::vector<std::pair<float, size_t>> draw_order;
  for (size_t rank = 0; rank < order.size(); ++rank) {
    const float target_y = list_top + rank * card_h;
    const float cy_anim =
        Approach(card_y_, key_base + "y" + std::to_string(order[rank]), target_y, 6.f);
    draw_order.push_back({std::fabs(cy_anim - target_y), rank});
  }
  std::sort(draw_order.begin(), draw_order.end());
  for (const auto& [moving, rank] : draw_order) {
    const int i = order[rank];
    y = card_y_[key_base + "y" + std::to_string(i)];
    const Candidate& cand = race->candidates[i];
    const Party& p = m.data->party(cand.party);
    const SkRect card = SkRect::MakeXYWH(x - 6 * s_, y + 4 * s_, w + 12 * s_, card_h - 6 * s_);
    const bool leader = counting && rank == 0;
    const bool elected = i == winner;
    const bool called = !elected && status && status->called == i;
    const bool conceded = status && status->conceded == i;
    c->drawRRect(SkRRect::MakeRectXY(card, 10 * s_, 10 * s_),
                 Fill(leader || elected ? SkColorSetARGB(46, 245, 197, 66)
                                        : SkColorSetARGB(18, 255, 255, 255)));
    // Gold glow when this candidate just took the lead / was projected.
    const std::string flash_key = race->id + ":" + std::to_string(i);
    const float flash = flash_.count(flash_key) ? flash_[flash_key] : 0.f;
    if (flash > 0) {
      SkPaint glow = Fill(SkColorSetARGB(static_cast<U8CPU>(220 * flash), 245, 197, 66));
      glow.setStyle(SkPaint::kStroke_Style);
      glow.setStrokeWidth((1.5f + 3 * flash) * s_);
      c->drawRRect(
          SkRRect::MakeRectXY(card.makeOutset(flash * 3 * s_, flash * 3 * s_), 12 * s_, 12 * s_),
          glow);
    }
    if (conceded) c->saveLayerAlphaf(nullptr, 0.55f);
    const float cy = card.fTop + (card.height() - photo) / 2;
    avatars_->Draw(c, cand, p, x + 2 * s_, cy, photo);

    const float tx = x + photo + 14 * s_;
    const float name_y = card.fTop + card.height() * 0.36f;
    float nx = tx;
    const SkFont name_font =
        fonts_->Bold(std::min(m.lang == Lang::kEn ? 17 * s_ : 20 * s_, card_h * 0.24f));
    const int64_t votes = static_cast<int64_t>(std::llround(shown[i]));
    const std::string votes_text = counting ? FormatThousands(votes) : "0";
    // Name and badges must stay clear of the vote count on the right.
    const float limit = x + w - TextWidth(fonts_->Bold(19 * s_), votes_text) - 10 * s_;
    nx += DrawText(c, Ellipsize(name_font, L_.CandidateName(cand), limit - nx - 50 * s_), nx,
                   name_y, name_font, kText) +
          8 * s_;
    const float chip_h = std::min(19 * s_, card_h * 0.22f);
    auto chip = [&](const std::string& text, uint32_t color, bool outline) {
      if (nx + ChipWidth(fonts_, text, chip_h) > limit) return;
      nx += Chip(c, fonts_, text, nx, name_y - chip_h * 0.8f, chip_h, color, s_, outline) + 5 * s_;
    };
    chip(L_.PartyShort(p), p.color, false);
    if (elected) {
      chip(L_.T("stamp.elected"), kWonGreen, false);
    } else if (called) {
      chip(L_.T("chip.called"), kCalledRed, false);
    } else if (leader && !race_final) {
      chip(L_.T("seats.leading"), kLeadAmber, false);
    }
    const bool revived = official ? cand.pr_elected : (!elected && RevivedNow(m, *race, cand));
    if (revived) chip(L_.T("chip.revived"), kRevived, false);
    if (!cand.status.empty()) {
      chip(L_.Status(cand.status), cand.status == "前" ? SkColor(kAccent) : kText2, true);
    }
    if (cand.dual) chip(L_.T("chip.dual"), 0xFF9FB0C3, true);
    if (status && status->declared == i && !elected) chip(L_.T("chip.declared"), 0xFF9B59B6, false);
    if (conceded) chip(L_.T("chip.conceded"), 0xFF7F8C8D, false);
    // Dual candidates under 10% of the district vote lose their list place.
    const int64_t cand_total = i < static_cast<int>(total.votes.size()) ? total.votes[i] : 0;
    const bool barred = cand.dual && !elected &&
                        (official ? cand.dual_disqualified
                                  : (Complete(total) && cand_total * 10 < total.TotalVotes()));
    if (barred) chip(L_.T("dash.chip.barred"), 0xFF7F8C8D, true);

    // Secondary line: reading / Japanese name, age, 惜敗率.
    std::string sub = L_.CandidateAltName(cand);
    if (cand.age > 0) {
      sub += (sub.empty() ? "" : " · ") + Fmt(L_.T("dash.age"), {std::to_string(cand.age)});
    }
    if (race_leader >= 0 && i != race_leader && i != winner) {
      double sek = official && cand.sekihairitsu > 0 ? cand.sekihairitsu
                                                      : election::Sekihairitsu(total, i);
      if (sek > 0) sub += " · " + Fmt(L_.T("dash.sekihai"), {Fixed(sek, 1) + "%"});
    }
    const float sub_w =
        DrawText(c, Ellipsize(fonts_->Regular(12 * s_), sub, w - photo - 110 * s_), tx,
                 name_y + 17 * s_, fonts_->Regular(12 * s_), kText3);
    DrawNewsCounts(c, m, cand.id, tx + sub_w + 8 * s_, name_y + 17 * s_);

    const float bar_y = card.fBottom - 16 * s_;
    const float share = shown_total > 0 ? static_cast<float>(shown[i] / shown_total) : 0.f;
    Bar(c, SkRect::MakeXYWH(tx, bar_y, w - photo - 16 * s_, 7 * s_), share, p.color);
    // Votes, right aligned.
    DrawText(c, votes_text, x + w, name_y, fonts_->Bold(19 * s_), kText, Align::kRight);
    DrawText(c, counting ? Percent(share) : "—", x + w, name_y + 18 * s_,
             fonts_->Regular(13 * s_), kText2, Align::kRight);
    // "+N" floaters: batches arriving in quick succession are merged.
    const std::string fkey = key_base + "f" + std::to_string(i);
    float_pending_[fkey] += added[i];
    float& cooldown = float_cooldown_[fkey];
    cooldown -= dt_;
    if (float_pending_[fkey] > 0 && cooldown <= 0) {
      floaters_.push_back({"+" + FormatThousands(static_cast<int64_t>(float_pending_[fkey])),
                           p.color, x + w - TextWidth(fonts_->Bold(19 * s_), votes_text) - 8 * s_,
                           name_y});
      float_pending_[fkey] = 0;
      cooldown = 0.9f;
    }
    if (conceded) c->restore();
    if (elected || called) {
      Seal(c, fonts_, L_.T(elected ? "stamp.elected" : "stamp.called"), x + w - 150 * s_,
           card.centerY(), std::min(58 * s_, card.height() * 0.8f),
           status ? status->called_age : 1e9f, kCalledRed);
    }
  }
  // Footnote.
  y = panel.fBottom - pad - 4 * s_;
  const std::string note =
      counting ? Fmt(L_.T("dash.race.totals"),
                     {FormatThousands(t.TotalVotes()), FormatThousands(t.eligible)})
               : std::string(L_.T("dash.race.order_note"));
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), note, w * 0.6f), x, y, fonts_->Regular(12 * s_),
           kText3);
  DrawText(c, L_.T("dash.pref.bloc_hint"), x + w, y, fonts_->Regular(11.5f * s_), kText3,
           Align::kRight);
}

void Dashboard::DrawBlocPanel(SkCanvas* c, const DashboardModel& m, SkRect panel,
                              const Race& pr) {
  Panel(c, panel, 14 * s_);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  const bool official = OfficialFinal(m);
  const election::PrOutcome* outcome = m.seats ? m.seats->Pr(&pr) : nullptr;

  const SkFont title_font = fonts_->Bold(m.lang == Lang::kEn ? 21 * s_ : 23 * s_);
  const std::string seats_text = Fmt(L_.T("dash.seats_n"), {std::to_string(pr.seats)});
  const float seats_w = TextWidth(fonts_->Bold(15 * s_), seats_text);
  DrawText(c, Ellipsize(title_font, L_.RaceTitle(pr), w - seats_w - 10 * s_), x, y, title_font,
           kText);
  DrawText(c, seats_text, x + w, y, fonts_->Bold(15 * s_), kAccent, Align::kRight);
  y += 22 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(13 * s_), L_.T("dash.bloc.sub"), w), x, y,
           fonts_->Regular(13 * s_), kAccent);
  y += 18 * s_;
  std::string prefs;
  for (const std::string& code : pr.prefs) {
    const int id = m.tree->FindByCode(code);
    if (id < 0) continue;
    prefs += (prefs.empty() ? "" : L_.ListSeparator()) + L_.RegionName(m.tree->region(id));
  }
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), prefs, w), x, y, fonts_->Regular(12 * s_),
           kText3);
  y += 12 * s_;

  const Tally& t = m.results->RaceTotal(pr);
  const bool counting = HasVotes(t);
  Bar(c, SkRect::MakeXYWH(x, y, w, 6 * s_), static_cast<float>(t.Progress()), kAccent);
  y += 20 * s_;
  DrawText(c,
           t.units_total > 0 ? Fmt(L_.T("dash.race.progress"), {std::to_string(t.units_counted),
                                                                 std::to_string(t.units_total),
                                                                 Percent(t.Progress(), 0)})
                             : std::string(L_.T("dash.race.not_started")),
           x, y, fonts_->Regular(12.5f * s_), kText2);
  if (counting) {
    DrawText(c, Fmt(L_.T("dash.votes_n"), {FormatThousands(t.TotalVotes())}), x + w, y,
             fonts_->Regular(12.5f * s_), kText2, Align::kRight);
  }
  y += 10 * s_;

  std::vector<int> order(pr.candidates.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  if (counting) order = t.Ranking();

  // Elected names flow under each list; revived district candidates in gold.
  struct Name {
    std::string text;
    bool revived;
  };
  const SkFont name_font = fonts_->Regular(11.5f * s_);
  const float line_h = 15 * s_;
  auto layout_names = [&](const std::vector<Name>& names, float nx, float ny, float nw,
                          int max_lines, bool draw) {
    int lines = names.empty() ? 0 : 1;
    float cx = nx;
    const std::string sep = L_.ListSeparator();
    for (size_t k = 0; k < names.size(); ++k) {
      const std::string text = (names[k].revived ? "◆" : "") + names[k].text +
                               (k + 1 < names.size() ? sep : "");
      const float tw = TextWidth(name_font, text);
      if (cx + tw > nx + nw && cx > nx) {
        if (lines >= max_lines) {
          if (draw) DrawText(c, "…", cx, ny + (lines - 1) * line_h, name_font, kText3);
          return lines;
        }
        ++lines;
        cx = nx;
      }
      if (draw) {
        DrawText(c, text, cx, ny + (lines - 1) * line_h, name_font,
                 names[k].revived ? kAccent : kText2);
      }
      cx += tw;
    }
    return lines;
  };
  std::vector<std::vector<Name>> names(pr.candidates.size());
  std::vector<int> seats(pr.candidates.size(), 0);
  int total_lines = 0;
  const float name_x = x + 18 * s_, name_w = w - 18 * s_;
  for (size_t li = 0; li < pr.candidates.size(); ++li) {
    seats[li] = ListSeats(m, pr, static_cast<int>(li), official);
    const Candidate& list = pr.candidates[li];
    for (int k : ElectedEntries(m, pr, static_cast<int>(li), official)) {
      if (k < 0 || k >= static_cast<int>(list.list.size())) continue;
      const ListEntry& e = list.list[k];
      names[li].push_back({L_.ListEntryName(e), !e.candidate.empty() || !e.smd.empty()});
    }
    total_lines += layout_names(names[li], name_x, 0, name_w, 99, false);
  }
  const float footer_h = 44 * s_;
  const int lists_shown = static_cast<int>(pr.candidates.size());
  const float base_h = 38 * s_;  // header row + bar per list
  const float budget = panel.fBottom - pad - footer_h - y - lists_shown * base_h;
  const int max_lines =
      total_lines * line_h <= budget ? 99 : std::max(1, static_cast<int>(budget / line_h / 5));

  for (int li : order) {
    const Candidate& list = pr.candidates[li];
    const Party& p = m.data->party(list.party);
    const int n = seats[li];
    if (y + base_h > panel.fBottom - pad - footer_h) break;
    y += 22 * s_;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y - 11 * s_, 12 * s_, 12 * s_), 3 * s_,
                                     3 * s_),
                 Fill(p.color));
    DrawText(c, Ellipsize(fonts_->Bold(14 * s_), L_.PartyName(p), w * 0.45f), x + 18 * s_, y,
             fonts_->Bold(14 * s_), n > 0 ? kText : kText2);
    const double share = counting ? t.Share(li) : 0.0;
    const int64_t votes = li < static_cast<int>(t.votes.size()) ? t.votes[li] : 0;
    DrawText(c, counting ? FormatThousands(votes) + "  " + Percent(share) : "—", x + w - 62 * s_, y,
             fonts_->Regular(12.5f * s_), kText2, Align::kRight);
    const std::string seat_text = std::to_string(n);
    const float sw = DrawText(c, L_.T("dash.seats_unit"), x + w, y,
                              fonts_->Regular(11 * s_), kText3, Align::kRight);
    DrawText(c, seat_text, x + w - sw - 3 * s_, y + 1 * s_, fonts_->Bold(18 * s_),
             n > 0 ? kText : kText3, Align::kRight);
    y += 5 * s_;
    const double shown = Roll("pr:" + pr.id + ":" + std::to_string(li), share * 10000) / 10000;
    Bar(c, SkRect::MakeXYWH(x + 18 * s_, y, w - 90 * s_, 5 * s_), static_cast<float>(shown),
        p.color);
    y += 11 * s_;
    if (!names[li].empty()) {
      const float remaining = panel.fBottom - pad - footer_h - y;
      const int allowed = std::max(1, std::min(max_lines, static_cast<int>(remaining / line_h)));
      const int used = layout_names(names[li], name_x, y + 2 * s_, name_w, allowed, true);
      y += used * line_h;
    }
  }

  // Footer: forfeited seats + legend.
  float fy = panel.fBottom - pad - footer_h + 16 * s_;
  Separator(c, x, fy - 12 * s_, w);
  std::string forfeit;
  if (outcome && outcome->forfeited > 0) {
    for (const auto& lo : outcome->lists) {
      const int lost = lo.uncapped_seats - lo.seats;
      if (lost <= 0 || lo.list < 0) continue;
      const Party& p = m.data->party(pr.candidates[lo.list].party);
      forfeit += (forfeit.empty() ? "" : " · ") +
                 Fmt(L_.T("dash.bloc.forfeited"), {L_.PartyShort(p), std::to_string(lost)});
    }
  }
  if (!forfeit.empty()) {
    DrawText(c, Ellipsize(fonts_->Bold(12 * s_), forfeit, w), x, fy, fonts_->Bold(12 * s_),
             kLeadAmber);
    fy += 17 * s_;
  }
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), L_.T("dash.bloc.legend"), w), x, fy,
           fonts_->Regular(11.5f * s_), kText3);
}

void Dashboard::DrawReviewPanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const auto& refs = m.data->info().referendums;
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  DrawText(c, Ellipsize(fonts_->Bold(21 * s_), L_.T("review.title"), w), x, y,
           fonts_->Bold(21 * s_), kText);
  y += 22 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(13 * s_),
                        Fmt(L_.T("dash.review.sub"), {std::to_string(refs.size())}), w),
           x, y, fonts_->Regular(13 * s_), kAccent);
  y += 12 * s_;
  const geo::Region& focus = m.tree->region(m.focus);
  const int pref = focus.level == geo::Level::kNation
                       ? -1
                       : m.tree->AncestorAt(m.focus, geo::Level::kCounty);
  const int nation = m.tree->nation().id;

  auto bar_row = [&](const std::string& label, const RefTally& t, float* yy) {
    const bool has = t.agree + t.disagree > 0;
    *yy += 20 * s_;
    DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), label, 86 * s_), x + 12 * s_, *yy,
             fonts_->Regular(12.5f * s_), kText2);
    const SkRect bar = SkRect::MakeXYWH(x + 104 * s_, *yy - 9 * s_, w - 104 * s_ - 76 * s_, 9 * s_);
    const float share = has ? static_cast<float>(t.AgreeShare()) : 0.f;
    const float shown = Approach(anim_, "review:" + label + std::to_string(t.agree), share, 4.f);
    Bar(c, bar, shown, kDismiss);
    SkPaint mid = Fill(SkColorSetARGB(210, 255, 255, 255));
    mid.setStrokeWidth(1.5f * s_);
    c->drawLine(bar.centerX(), bar.fTop - 3 * s_, bar.centerX(), bar.fBottom + 3 * s_, mid);
    DrawText(c, has ? Percent(t.AgreeShare()) : "—", x + w, *yy, fonts_->Bold(14 * s_),
             has ? SkColor(kDismiss) : kText3, Align::kRight);
  };

  const float avail = panel.fBottom - pad - 70 * s_ - y;
  const float card_h = std::min(250 * s_, avail / std::max<size_t>(1, refs.size()));
  for (size_t i = 0; i < refs.size(); ++i) {
    const auto& ref = refs[i];
    const SkRect card = SkRect::MakeXYWH(x - 6 * s_, y + 10 * s_, w + 12 * s_, card_h - 10 * s_);
    const bool selected = static_cast<int>(i) == m.review;
    c->drawRRect(SkRRect::MakeRectXY(card, 10 * s_, 10 * s_),
                 Fill(selected ? SkColorSetARGB(40, 245, 197, 66) : SkColorSetARGB(18, 255, 255, 255)));
    if (selected) {
      SkPaint border = Fill(SkColorSetARGB(150, 245, 197, 66));
      border.setStyle(SkPaint::kStroke_Style);
      border.setStrokeWidth(1.2f * s_);
      c->drawRRect(SkRRect::MakeRectXY(card, 10 * s_, 10 * s_), border);
    }
    float ty = card.fTop + 28 * s_;
    const RefTally& nat = m.results->ReferendumTally(ref.id, nation);
    // Verdict chip (once the national count is complete).
    std::string verdict;
    uint32_t verdict_color = kLeadAmber;
    if (nat.agree + nat.disagree > 0) {
      if (nat.Progress() >= 1) {
        verdict = L_.T(nat.Passes() ? "review.dismissed" : "review.retained");
        verdict_color = nat.Passes() ? kDismiss : kRetain;
      } else {
        verdict = L_.T("status.counting");
      }
    }
    const float vw = verdict.empty() ? 0 : ChipWidth(fonts_, verdict, 22 * s_);
    DrawText(c, Ellipsize(fonts_->Bold(20 * s_), L_.Justice(ref), w - vw - 20 * s_), x + 6 * s_,
             ty, fonts_->Bold(20 * s_), kText);
    if (!verdict.empty()) {
      Chip(c, fonts_, verdict, x + w - vw, ty - 17 * s_, 22 * s_, verdict_color, s_);
    }
    ty += 19 * s_;
    DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), L_.Bio(ref), w - 12 * s_), x + 6 * s_, ty,
             fonts_->Regular(12.5f * s_), kText2);
    const SkFont qf = fonts_->Regular(12 * s_);
    std::vector<std::string> lines = WrapText(qf, L_.Question(ref), w - 12 * s_);
    if (lines.size() > 2) lines.resize(2);
    for (const std::string& line : lines) {
      ty += 16 * s_;
      DrawText(c, line, x + 6 * s_, ty, qf, kText3);
    }
    ty += 4 * s_;
    bar_row(L_.T("level.nation"), nat, &ty);
    if (pref >= 0) {
      bar_row(L_.RegionName(m.tree->region(pref)), m.results->ReferendumTally(ref.id, pref), &ty);
    }
    if (nat.agree + nat.disagree > 0 && ty + 20 * s_ < card.fBottom) {
      ty += 20 * s_;
      DrawText(c,
               Ellipsize(fonts_->Regular(11.5f * s_),
                         std::string(L_.T("review.agree")) + " " + FormatThousands(nat.agree) +
                             " · " + L_.T("review.disagree") + " " + FormatThousands(nat.disagree),
                         w - 12 * s_),
               x + 12 * s_, ty, fonts_->Regular(11.5f * s_), kText3);
    }
    y += card_h;
  }
  // Rules.
  float fy = panel.fBottom - pad - 40 * s_;
  Separator(c, x, fy, w);
  fy += 18 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), L_.T("review.rule"), w), x, fy,
           fonts_->Regular(12 * s_), kText2);
  fy += 17 * s_;
  std::string marks = L_.T("dash.review.marks");
  if (pref >= 0) marks += " · " + std::string(L_.T("review.prefecture_level"));
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), marks, w), x, fy,
           fonts_->Regular(11.5f * s_), kText3);
}

void Dashboard::DrawLabels(SkCanvas* c, const DashboardModel& m) {
  std::vector<MapLabel> labels = m.labels;
  std::sort(labels.begin(), labels.end(),
            [](const MapLabel& a, const MapLabel& b) { return a.priority > b.priority; });
  std::vector<SkRect> placed;
  const float panel_x = Layout(m.width, m.height).panel_x;
  for (const MapLabel& l : labels) {
    const geo::Region& r = m.tree->region(l.region);
    const bool county = r.level == geo::Level::kCounty;
    const bool hovered = l.region == m.hover;
    const SkFont f = county || hovered ? fonts_->Bold((county ? 15 : 13.5f) * s_)
                                       : fonts_->Regular(13 * s_);
    const std::string name = L_.RegionName(r);
    const float tw = TextWidth(f, name);
    const SkRect rect = SkRect::MakeXYWH(l.x - tw / 2 - 3 * s_, l.y - 14 * s_, tw + 6 * s_,
                                         18 * s_);
    if (rect.fRight > panel_x || rect.fLeft < 0 || rect.fTop < 0 || rect.fBottom > m.height) {
      continue;
    }
    bool overlaps = false;
    for (const SkRect& p : placed) {
      if (SkRect::Intersects(p, rect)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps && !hovered) continue;
    placed.push_back(rect);
    SkPaint halo;
    halo.setAntiAlias(true);
    halo.setStyle(SkPaint::kStroke_Style);
    halo.setStrokeWidth(3.5f * s_);
    halo.setStrokeJoin(SkPaint::kRound_Join);
    halo.setColor(SkColorSetARGB(200, 5, 12, 22));
    const float x0 = l.x - tw / 2;
    c->drawSimpleText(name.data(), name.size(), SkTextEncoding::kUTF8, x0, l.y, f, halo);
    DrawText(c, name, x0, l.y, f, hovered ? kAccent : kText);
  }
}

void Dashboard::DrawInsets(SkCanvas* c, const DashboardModel& m) {
  for (const InsetFrame& f : m.insets) {
    SkPathBuilder b;
    b.moveTo(f.x[0], f.y[0]);
    for (int k = 1; k < 4; ++k) b.lineTo(f.x[k], f.y[k]);
    b.close();
    SkPaint p = Fill(SkColorSetARGB(150, 160, 176, 195));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.2f * s_);
    const float intervals[2] = {6 * s_, 5 * s_};
    p.setPathEffect(SkDashPathEffect::Make(intervals, 0));
    c->drawPath(b.detach(), p);
    // Label under the bottom-left corner: region name + "not to position".
    std::string label = L_.T("inset");
    if (f.region >= 0) label = L_.RegionName(m.tree->region(f.region)) + " · " + label;
    DrawText(c, label, f.x[3] + 4 * s_, f.y[3] + 13 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetARGB(190, 160, 176, 195));
  }
}

void Dashboard::DrawTooltip(SkCanvas* c, const DashboardModel& m) {
  if (m.hover < 0) return;
  const geo::Region& r = m.tree->region(m.hover);
  if (r.level == geo::Level::kNation) return;
  const float pad = 10 * s_;
  const float w = 270 * s_;
  const char* level = L_.T(r.level == geo::Level::kCounty ? "level.county"
                           : r.level == geo::Level::kTown ? "level.town"
                                                          : "level.village");
  std::string title = L_.RegionName(r);
  if (r.level == geo::Level::kVillage) {
    const int district = m.tree->AncestorAt(m.hover, geo::Level::kTown);
    if (district >= 0) {
      const std::string parent = L_.RegionName(m.tree->region(district));
      title = m.lang == Lang::kEn ? title + ", " + parent : parent + " " + title;
    }
  }
  const std::string alt = m.lang == Lang::kEn ? r.name_ja : r.name_en;

  // Body lines: coloured dot, left text, right value.
  struct Line {
    uint32_t dot = 0;
    std::string left, right;
  };
  std::vector<Line> lines;
  std::string note;
  if (m.mode == ColorMode::kReview) {
    for (const auto& ref : m.data->info().referendums) {
      const RefTally& t = m.results->ReferendumTally(ref.id, m.hover);
      lines.push_back({kDismiss, L_.Justice(ref),
                       t.agree + t.disagree > 0
                           ? Fmt(L_.T("review.dismiss_share"), {Percent(t.AgreeShare())})
                           : "—"});
    }
    if (r.level != geo::Level::kCounty) note = L_.T("review.prefecture_level");
  } else if (m.mode == ColorMode::kPr) {
    if (const Race* pr = m.results->PrRaceForRegion(m.hover)) {
      const Tally& t = m.results->RaceTally(*pr, m.hover);
      note = L_.RaceTitle(*pr);
      if (HasVotes(t)) {
        const std::vector<int> rank = t.Ranking();
        for (size_t k = 0; k < std::min<size_t>(4, rank.size()); ++k) {
          const Party& p = m.data->party(pr->candidates[rank[k]].party);
          lines.push_back({p.color, L_.PartyShort(p), Percent(t.Share(rank[k]))});
        }
      }
    }
  } else if (r.level == geo::Level::kCounty) {
    const PrefSummary s = SummarizePref(m, r);
    if (m.mode == ColorMode::kTurnout && s.eligible > 0 && s.ballots > 0) {
      lines.push_back({kTurnoutColor, L_.T("mode.turnout"),
                       Percent(static_cast<double>(s.ballots) / static_cast<double>(s.eligible))});
    } else if (m.mode == ColorMode::kMargin && s.with_votes) {
      lines.push_back({MarginColor(s.margin), L_.T("dash.tip.avg_margin"), Percent(s.margin)});
    }
    if (!s.parties.empty()) note = L_.T("dash.tip.districts_led");
    for (size_t k = 0; k < std::min<size_t>(4, s.parties.size()); ++k) {
      const Party& p = m.data->party(s.parties[k].first);
      lines.push_back({p.color, L_.PartyShort(p),
                       std::to_string(s.parties[k].second) + " / " + std::to_string(s.districts)});
    }
  } else if (const Race* race = m.results->SmdRaceForRegion(m.hover)) {
    const Tally& t = m.results->RaceTally(*race, m.hover);
    if (r.level == geo::Level::kVillage) note = L_.RaceTitle(*race);
    if (HasVotes(t)) {
      if (m.mode == ColorMode::kTurnout && t.eligible > 0 && t.ballots_cast > 0) {
        lines.push_back({kTurnoutColor, L_.T("mode.turnout"), Percent(t.Turnout())});
      } else if (m.mode == ColorMode::kMargin) {
        const std::vector<int> rank = t.Ranking();
        const int64_t diff = rank.size() > 1 ? t.votes[rank[0]] - t.votes[rank[1]] : t.votes[rank[0]];
        lines.push_back({MarginColor(MarginShare(t)), L_.T("mode.margin"),
                         Percent(MarginShare(t)) + " · " +
                             Fmt(L_.T("dash.margin_votes"), {FormatThousands(diff)})});
      }
      const std::vector<int> rank = t.Ranking();
      for (size_t k = 0; k < std::min<size_t>(3, rank.size()); ++k) {
        const Candidate& cand = race->candidates[rank[k]];
        const Party& p = m.data->party(cand.party);
        lines.push_back({p.color, L_.CandidateName(cand) + "  " + L_.PartyShort(p),
                         Percent(t.Share(rank[k]))});
      }
    } else {
      const int called = CalledOf(m, *race);
      if (called >= 0) {
        const Candidate& cand = race->candidates[called];
        const Party& p = m.data->party(cand.party);
        lines.push_back({p.color, L_.CandidateName(cand) + "  " + L_.PartyShort(p),
                         L_.T("stamp.called")});
      }
    }
  }
  const bool empty = lines.empty();
  float h = pad * 2 + 40 * s_ + (note.empty() ? 0 : 18 * s_);
  h += empty ? 20 * s_ : 21 * s_ * lines.size() + 8 * s_;
  float x = m.mouse_x + 18 * s_;
  float y = m.mouse_y + 18 * s_;
  if (x + w > Layout(m.width, m.height).panel_x - 8 * s_) x = m.mouse_x - w - 18 * s_;
  if (y + h > m.height - 30 * s_) y = m.mouse_y - h - 12 * s_;
  const SkRect box = SkRect::MakeXYWH(x, y, w, h);
  Panel(c, box, 10 * s_);
  float ty = y + pad + 16 * s_;
  const float level_w = TextWidth(fonts_->Regular(11.5f * s_), level) + 8 * s_;
  DrawText(c, Ellipsize(fonts_->Bold(16 * s_), title, w - 2 * pad - level_w), x + pad, ty,
           fonts_->Bold(16 * s_), kText);
  DrawText(c, level, x + w - pad, ty, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  ty += 18 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), alt, w - 2 * pad), x + pad, ty,
           fonts_->Regular(11.5f * s_), kText3);
  if (!note.empty()) {
    ty += 18 * s_;
    DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), note, w - 2 * pad), x + pad, ty,
             fonts_->Regular(11.5f * s_), kText2);
  }
  if (!empty) {
    ty += 6 * s_;
    for (const Line& line : lines) {
      ty += 21 * s_;
      if (line.dot) c->drawCircle(x + pad + 5 * s_, ty - 5 * s_, 5 * s_, Fill(line.dot));
      const float rw = TextWidth(fonts_->Bold(13 * s_), line.right);
      DrawText(c, Ellipsize(fonts_->Regular(13 * s_), line.left, w - 2 * pad - 22 * s_ - rw),
               x + pad + 16 * s_, ty, fonts_->Regular(13 * s_), kText);
      DrawText(c, line.right, x + w - pad, ty, fonts_->Bold(13 * s_), kText, Align::kRight);
    }
  } else {
    ty += 22 * s_;
    const bool can_enter = !r.children.empty();
    DrawText(c, L_.T(can_enter ? "tip.no_votes_enter" : "tip.no_votes"), x + pad, ty,
             fonts_->Regular(12.5f * s_), kText2);
  }
}

void Dashboard::DrawFooter(SkCanvas* c, const DashboardModel& m) {
  const float y = m.height - 12 * s_;
  const SkFont f = fonts_->Regular(12 * s_);
  const float lang_w = DrawText(c, std::string("◐ ") + LangNativeName(m.lang), 16 * s_, y,
                                fonts_->Bold(12 * s_), kAccent) +
                       12 * s_;
  DrawText(c,
           Ellipsize(f, Fmt(L_.T("footer.controls"), {ColorModeName(m.lang, m.mode)}),
                     m.width * 0.62f - lang_w),
           16 * s_ + lang_w, y, f, kText2);
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.0f fps · ", m.fps);
  std::string right = buf + m.gpu + " · " + m.source;
  if (!m.source_error.empty()) right = "⚠ " + m.source_error + " · " + right;
  DrawText(c, Ellipsize(f, right, m.width * 0.34f), m.width - 16 * s_, y, f,
           m.source_error.empty() ? kText3 : SkColorSetRGB(255, 120, 100), Align::kRight);

  // Credits under the right panel: map sources and candidate photos.
  const DashboardLayout layout = Layout(m.width, m.height);
  const float cw = m.width - 16 * s_ - layout.panel_x;
  const SkFont cf = fonts_->Regular(10.5f * s_);
  const std::string map_src = MapCredits(avatars_->root());
  if (!map_src.empty()) {
    DrawText(c, Ellipsize(cf, Fmt(L_.T("credits.map"), {map_src}), cw), m.width - 16 * s_,
             m.height - 44 * s_, cf, kText3, Align::kRight);
  }
  DrawText(c, Ellipsize(cf, L_.T("credits.photos"), cw), m.width - 16 * s_, m.height - 30 * s_, cf,
           kText3, Align::kRight);

  // Legend (bottom-left, above the footer).
  float lx = 16 * s_;
  const float ly = m.height - 40 * s_;
  const float lmax = layout.panel_x - 24 * s_;
  if (m.mode == ColorMode::kLeader || m.mode == ColorMode::kPr) {
    std::vector<std::string> shown;
    if (m.seats) {
      std::vector<const election::PartySeats*> ps;
      for (const auto& p : m.seats->parties) {
        if (m.mode == ColorMode::kLeader ? p.smd > 0 : p.pr_votes > 0) ps.push_back(&p);
      }
      std::stable_sort(ps.begin(), ps.end(), [&](const auto* a, const auto* b) {
        return m.mode == ColorMode::kLeader ? a->smd > b->smd : a->pr_votes > b->pr_votes;
      });
      for (const auto* p : ps) shown.push_back(p->party);
    }
    if (shown.empty()) {
      for (const Party& p : m.data->parties()) shown.push_back(p.code);
    }
    DrawText(c, ColorModeName(m.lang, m.mode), lx, ly - 16 * s_, fonts_->Regular(11 * s_), kText3);
    for (const std::string& code : shown) {
      const Party& p = m.data->party(code);
      const std::string name = L_.PartyShort(p);
      const float iw = 16 * s_ + TextWidth(fonts_->Regular(12 * s_), name) + 12 * s_;
      if (lx + iw > lmax) break;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, 12 * s_, 12 * s_),
                                       3 * s_, 3 * s_),
                   Fill(p.color));
      lx += 16 * s_;
      lx += DrawText(c, name, lx, ly, fonts_->Regular(12 * s_), kText2) + 12 * s_;
    }
    return;
  }
  const float gw = 160 * s_;
  SkPaint p;
  p.setAntiAlias(true);
  const SkPoint pts[2] = {{lx, 0}, {lx + gw, 0}};
  std::string lo_txt, hi_txt, name = ColorModeName(m.lang, m.mode);
  if (m.mode == ColorMode::kMargin) {
    const SkColor4f cols[3] = {SkColor4f::FromColor(kClose), SkColor4f::FromColor(kMid),
                               SkColor4f::FromColor(kSafe)};
    p.setShader(SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors(cols, {}, SkTileMode::kClamp), {})));
    lo_txt = std::string(L_.T("dash.legend.close")) + " 0%";
    hi_txt = Percent(kMarginSafe, 0) + "+ " + L_.T("dash.legend.safe");
  } else {
    const uint32_t hi = m.mode == ColorMode::kTurnout ? kTurnoutColor : kDismiss;
    const SkColor4f cols[2] = {SkColor4f::FromColor(MixColor(kNeutral, hi, 0.1f)),
                               SkColor4f::FromColor(hi)};
    p.setShader(SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors(cols, {}, SkTileMode::kClamp), {})));
    if (m.mode == ColorMode::kTurnout) {
      lo_txt = Percent(kTurnoutLo, 0);
      hi_txt = Percent(kTurnoutHi, 0);
    } else {
      lo_txt = Percent(kDismissLo, 0);
      hi_txt = Percent(kDismissHi, 0) + "+";
      const auto& refs = m.data->info().referendums;
      if (!refs.empty()) {
        const int idx = std::clamp(m.review, 0, static_cast<int>(refs.size()) - 1);
        name += " · " + L_.Justice(refs[idx]) + " · " + L_.T("review.agree");
      }
    }
  }
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, gw, 12 * s_), 4 * s_, 4 * s_),
               p);
  DrawText(c, lo_txt, lx, ly - 14 * s_, fonts_->Regular(11 * s_), kText3);
  DrawText(c, hi_txt, lx + gw, ly - 14 * s_, fonts_->Regular(11 * s_), kText3, Align::kRight);
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), name, lmax - lx - gw - 10 * s_), lx + gw + 10 * s_,
           ly, fonts_->Regular(12 * s_), kText2);
}

void Dashboard::DrawHelp(SkCanvas* c, const DashboardModel& m) {
  const float w = 640 * s_;
  const char* keys[] = {"help.1", "help.2", "help.3", "help.4", "help.5",
                        "help.6", "help.7", "help.8", "help.9", "help.10"};
  const SkFont f = fonts_->Regular(15 * s_);
  std::vector<std::string> lines;
  for (const char* k : keys) {
    std::vector<std::string> wrapped = WrapText(f, L_.T(k), w - 48 * s_);
    for (size_t i = 0; i < wrapped.size(); ++i) {
      lines.push_back(i == 0 ? wrapped[i] : "    " + wrapped[i]);
    }
  }
  const float h = 42 * s_ + 38 * s_ + lines.size() * 26 * s_ + 10 * s_;
  const float x = std::max(8 * s_, (Layout(m.width, m.height).panel_x - w) / 2);
  const float y = std::max(8 * s_, (m.height - h) / 2);
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 14 * s_);
  float ty = y + 42 * s_;
  DrawText(c, L_.T("help.title"), x + 24 * s_, ty, fonts_->Bold(20 * s_), kAccent);
  ty += 38 * s_;
  for (const std::string& line : lines) {
    DrawText(c, line, x + 24 * s_, ty, f, kText);
    ty += 26 * s_;
  }
}

}  // namespace jpy::ui
