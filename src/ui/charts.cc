// Secondary (non-map) chart views and the picture-in-picture "latest" window.
//
//   F2 得票推移   Trend     vote share over 20:00-04:00 of one race (district
//                           candidates or a PR bloc's party lists) + the seat
//                           projection over the night vs the majority (233)
//   F3 議席図     Seats     465-seat hemicycle, parties left to right, decided
//                           solid / leading faded, majority 233 and 2/3 310
//   F4 接戦順     Margins   closest districts first, bar-chart race
//   F5 政党別     Parties   nationwide PR votes by party with SMD + PR seats
//   F6 選挙区一覧 Districts all 289 districts as tiles, grouped by PR bloc
//
// The map stays the primary view: charts slide in over it (the map keeps
// animating, dimmed, behind) and M / Esc / the 地図 tab return to it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "src/election/events.h"
#include "src/election/results_source.h"
#include "src/ui/dashboard.h"

namespace jpy::ui {

using election::Candidate;
using election::Race;
using election::Tally;

namespace {

constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kGold = SkColorSetRGB(245, 197, 66);
constexpr SkColor kRed = SkColorSetRGB(214, 48, 49);
constexpr SkColor kGrid = SkColorSetARGB(40, 255, 255, 255);
constexpr uint32_t kEmptySeat = 0xFF2B3D52;

// The counting night on the chart axes: 20:00 -> 04:00.
constexpr double kNight = election::SimulatedResultsSource::kCountMinutes;

SkPaint P(SkColor c, float alpha = 1) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  p.setAlphaf(std::clamp(alpha, 0.f, 1.f) * SkColorGetA(c) / 255.f);
  return p;
}

SkPaint Stroke(SkColor c, float w, float alpha = 1) {
  SkPaint p = P(c, alpha);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(w);
  p.setStrokeCap(SkPaint::kRound_Cap);
  p.setStrokeJoin(SkPaint::kRound_Join);
  return p;
}

SkPaint Dashed(SkColor c, float w, float on, float off, float alpha = 1) {
  SkPaint p = Stroke(c, w, alpha);
  const float iv[2] = {on, off};
  p.setPathEffect(SkDashPathEffect::Make(iv, 0));
  return p;
}

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

float EaseBack(float t) {  // slight overshoot for "pop"
  t = std::clamp(t, 0.f, 1.f);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * std::pow(t - 1, 3.f) + c1 * std::pow(t - 1, 2.f);
}

std::string Pct(double v, int d = 1) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%.*f%%", d, v * 100);
  return buf;
}

uint32_t InkOn(uint32_t bg) { return Luminance(bg) > 0.62f ? 0xFF10161E : 0xFFFFFFFF; }

// Seating order in the chamber, left to right. Parties not listed sit in
// the middle (with independents and undecided seats).
int SeatOrder(const std::string& party) {
  static const char* kLeft[] = {"JCP", "REIWA", "SDP", "CRA", "MIRAI", "DPFP"};
  static const char* kRight[] = {"JIP", "LDP", "GENZEI", "CPJ", "SANSEI"};
  for (int i = 0; i < 6; ++i) {
    if (party == kLeft[i]) return i;
  }
  for (int i = 0; i < 5; ++i) {
    if (party == kRight[i]) return 20 + i;
  }
  return party == "IND" ? 12 : 10;
}
constexpr int kUndecidedOrder = 15;  // grey seats: between the centre and the right

// A race's overall state (its whole district / bloc).
struct RaceState {
  const Race* race = nullptr;
  const Tally* tally = nullptr;
  int leader = -1, second = -1;
  double margin = 0;  // share points
  int64_t margin_votes = 0;
  bool has_votes = false;
};

RaceState StateOf(const DashboardModel& m, const Race& race) {
  RaceState s;
  s.race = &race;
  s.tally = &m.results->RaceTotal(race);
  if (s.tally->TotalVotes() <= 0) return s;
  const std::vector<int> rank = s.tally->Ranking();
  s.has_votes = true;
  s.leader = rank[0];
  s.second = rank.size() > 1 ? rank[1] : -1;
  s.margin = s.tally->Share(s.leader) - (s.second >= 0 ? s.tally->Share(s.second) : 0);
  s.margin_votes = s.tally->votes[s.leader] - (s.second >= 0 ? s.tally->votes[s.second] : 0);
  return s;
}

// District number inside a prefecture: "13-01" -> "1".
std::string DistrictNumber(const Race& race) {
  const size_t dash = race.id.rfind('-');
  std::string n = dash == std::string::npos ? race.id : race.id.substr(dash + 1);
  while (n.size() > 1 && n[0] == '0') n.erase(0, 1);
  return n;
}

// "北陸信越ブロック" -> "北陸信越" for compact headers.
std::string ShortBloc(std::string name) {
  for (const char* suffix : {"ブロック", "區塊", " bloc", " block"}) {
    const size_t len = std::char_traits<char>::length(suffix);
    if (name.size() > len && name.compare(name.size() - len, len, suffix) == 0) {
      name.resize(name.size() - len);
    }
  }
  return name;
}

}  // namespace

const char* ChartKey(ChartKind k) {
  switch (k) {
    case ChartKind::kTrend: return "chart.trend";
    case ChartKind::kSeats: return "chart.seats";
    case ChartKind::kMargins: return "chart.margins";
    case ChartKind::kParties: return "chart.parties";
    case ChartKind::kGrid: return "chart.grid";
    default: return "chart.map";
  }
}

DashboardHit Dashboard::HitTest(float x, float y) const {
  // Last added wins (drawn on top).
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return {};
}

bool Dashboard::Captures(float x, float y) const {
  if (pip_rect_.contains(x, y)) return true;
  for (const auto& [r, hit] : hits_) {
    if (hit.kind == DashboardHit::Kind::kChartTab && r.contains(x, y)) return true;
  }
  return chart_t_ > 0.5f && last_chart_area_.contains(x, y);
}

SkRect Dashboard::ChartArea(const DashboardModel& m) const {
  const float panel_x = Layout(m.width, m.height).panel_x;
  // Below the header/breadcrumb, between the side columns, above the tabs.
  return SkRect::MakeLTRB(362 * s_, 128 * s_, panel_x - 16 * s_, m.height - 180 * s_);
}

void Dashboard::DrawChartTabs(SkCanvas* c, const DashboardModel& m) {
  const SkRect area = ChartArea(m);
  const SkFont f = fonts_->Bold(12.5f * s_);
  const float h = 26 * s_;
  std::vector<std::pair<std::string, float>> labels;
  float total = 0;
  for (int k = 0; k < static_cast<int>(ChartKind::kCount); ++k) {
    const std::string key = k == 0 ? "M" : "F" + std::to_string(k + 1);
    std::string label = std::string(L_.T(ChartKey(static_cast<ChartKind>(k)))) + "  " + key;
    const float w = TextWidth(f, label) + 22 * s_;
    labels.push_back({label, w});
    total += w + 4 * s_;
  }
  float x = area.centerX() - total / 2;
  const float y = area.fBottom + 10 * s_;
  const SkRect bar = SkRect::MakeXYWH(x - 6 * s_, y - 4 * s_, total + 8 * s_, h + 8 * s_);
  c->drawRRect(SkRRect::MakeRectXY(bar, 12 * s_, 12 * s_), P(SkColorSetARGB(200, 10, 20, 33)));
  const int active = static_cast<int>(m.chart);
  float active_x = x, active_w = 0;
  for (int k = 0; k < static_cast<int>(labels.size()); ++k) {
    const SkRect r = SkRect::MakeXYWH(x, y, labels[k].second, h);
    if (k == active) {
      active_x = x;
      active_w = labels[k].second;
    }
    AddHit(r, {DashboardHit::Kind::kChartTab, static_cast<ChartKind>(k), nullptr});
    x += labels[k].second + 4 * s_;
  }
  // Sliding highlight behind the active tab.
  const float hx = Approach(anim_, "tab_x", active_x, 10.f);
  const float hw = Approach(anim_, "tab_w", active_w, 10.f);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(hx, y, hw, h), 9 * s_, 9 * s_),
               P(active == 0 ? SkColorSetRGB(58, 110, 165) : SkColorSetRGB(224, 142, 11)));
  x = area.centerX() - total / 2;
  for (int k = 0; k < static_cast<int>(labels.size()); ++k) {
    DrawText(c, labels[k].first, x + 11 * s_, y + 17.5f * s_, f, k == active ? SK_ColorWHITE : kText2);
    x += labels[k].second + 4 * s_;
  }
}

void Dashboard::DrawChartView(SkCanvas* c, const DashboardModel& m) {
  // Open/close transition; switching between charts restarts the build-in.
  if (m.chart != ChartKind::kMap && m.chart != shown_chart_) {
    shown_chart_ = m.chart;
    chart_age_ = 0;
  }
  chart_t_ += ((m.chart != ChartKind::kMap ? 1.f : 0.f) - chart_t_) * (1 - std::exp(-dt_ * 9));
  chart_age_ += dt_;
  last_chart_area_ = ChartArea(m);
  if (chart_t_ < 0.01f || shown_chart_ == ChartKind::kMap) return;
  const SkRect area = last_chart_area_;
  const float t = Ease(chart_t_);
  c->save();
  // Rise + scale in from the tab bar.
  c->translate(area.centerX(), area.fBottom);
  const float sc = 0.94f + 0.06f * t;
  c->scale(sc, sc);
  c->translate(-area.centerX(), -area.fBottom + (1 - t) * 30 * s_);
  c->saveLayerAlphaf(nullptr, t);
  c->drawRRect(SkRRect::MakeRectXY(area, 16 * s_, 16 * s_), P(SkColorSetARGB(238, 9, 17, 29)));
  c->drawRRect(SkRRect::MakeRectXY(area.makeInset(0.5f, 0.5f), 16 * s_, 16 * s_),
               Stroke(SkColorSetARGB(60, 255, 255, 255), 1));
  const SkRect inner = area.makeInset(22 * s_, 18 * s_);
  switch (shown_chart_) {
    case ChartKind::kTrend: DrawTrendChart(c, m, inner); break;
    case ChartKind::kSeats: DrawSeatArc(c, m, inner); break;
    case ChartKind::kMargins: DrawMargins(c, m, inner); break;
    case ChartKind::kParties: DrawParties(c, m, inner); break;
    case ChartKind::kGrid: DrawRaceGrid(c, m, inner); break;
    default: break;
  }
  DrawText(c, L_.T("chart.back"), area.fRight - 18 * s_, area.fTop + 26 * s_,
           fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  c->restore();
  c->restore();
}

// ---------------------------------------------------------------- trend ----

void Dashboard::DrawTrendChart(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const Race* race = m.chart_race;
  DrawText(c, race ? Fmt(L_.T("trend.title"), {L_.RaceTitle(*race)}) : std::string(L_.T("chart.trend")),
           area.fLeft, area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);
  DrawText(c, L_.T("trend.hint"), area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12 * s_), kText3);
  // Upper part: vote share of the race; lower part: seats over the night.
  const float split = area.fTop + 64 * s_ + (area.height() - 64 * s_) * 0.56f;
  const SkRect plot = SkRect::MakeLTRB(area.fLeft + 44 * s_, area.fTop + 64 * s_,
                                       area.fRight - 150 * s_, split - 30 * s_);
  DrawSeatProjection(c, m, SkRect::MakeLTRB(area.fLeft, split, area.fRight, area.fBottom),
                     plot.fLeft, plot.fRight);
  auto X = [&](double minute) {
    return plot.fLeft + static_cast<float>(std::clamp(minute / kNight, 0.0, 1.0)) * plot.width();
  };
  for (int hr = 0; hr <= static_cast<int>(kNight / 60); ++hr) {
    DrawText(c, election::SimulatedResultsSource::ClockLabel(hr * 60.0), X(hr * 60.0),
             plot.fBottom + 16 * s_, fonts_->Regular(10.5f * s_), kText3, Align::kCenter);
  }
  const RaceHistory* h = nullptr;
  if (race && m.history) {
    auto it = m.history->find(race->id);
    if (it != m.history->end()) h = &it->second;
  }
  // Keep only samples with votes.
  std::vector<size_t> idx;
  if (h) {
    for (size_t i = 0; i < h->minutes.size() && i < h->votes.size(); ++i) {
      int64_t sum = 0;
      for (int64_t v : h->votes[i]) sum += v;
      if (sum > 0) idx.push_back(i);
    }
  }
  if (!race || idx.size() < 2) {
    DrawText(c, L_.T("chart.nodata"), plot.centerX(), plot.centerY(), fonts_->Regular(16 * s_), kText2,
             Align::kCenter);
    return;
  }
  auto share = [&](size_t i, int cand) {
    int64_t sum = 0;
    for (int64_t v : h->votes[i]) sum += v;
    return sum > 0 && cand < static_cast<int>(h->votes[i].size())
               ? static_cast<double>(h->votes[i][cand]) / sum
               : 0.0;
  };
  auto progress = [&](size_t i) { return i < h->progress.size() ? h->progress[i] : 0.f; };
  // Top candidates (or party lists) by latest votes.
  const std::vector<int64_t>& last = h->votes[idx.back()];
  std::vector<int> cands;
  for (size_t i = 0; i < last.size() && i < race->candidates.size(); ++i) cands.push_back(static_cast<int>(i));
  std::sort(cands.begin(), cands.end(), [&](int a, int b) { return last[a] > last[b]; });
  const size_t max_series = race->is_pr() ? 6 : 4;
  if (cands.size() > max_series) cands.resize(max_series);
  if (cands.empty()) return;
  // Y range (animated) from the visible series, after the first 5% counted.
  double lo = 1, hi = 0;
  for (size_t i : idx) {
    if (progress(i) < 0.05f && i != idx.back()) continue;
    for (int cnd : cands) {
      lo = std::min(lo, share(i, cnd));
      hi = std::max(hi, share(i, cnd));
    }
  }
  lo = std::max(0.0, std::floor((lo - 0.03) * 20) / 20);
  hi = std::min(1.0, std::ceil((hi + 0.03) * 20) / 20);
  const float y0 = Approach(anim_, "trend_lo:" + race->id, static_cast<float>(lo), 4);
  const float y1 = Approach(anim_, "trend_hi:" + race->id, static_cast<float>(hi), 4);
  auto Y = [&](double v) {
    return plot.fBottom - static_cast<float>((v - y0) / std::max(0.01f, y1 - y0)) * plot.height();
  };
  // Counting progress as a soft area at the bottom.
  {
    SkPathBuilder b;
    b.moveTo(X(h->minutes[idx.front()]), plot.fBottom);
    for (size_t i : idx) b.lineTo(X(h->minutes[i]), plot.fBottom - progress(i) * plot.height() * 0.25f);
    b.lineTo(X(h->minutes[idx.back()]), plot.fBottom);
    b.close();
    c->drawPath(b.detach(), P(SkColorSetRGB(88, 160, 230), 0.16f));
  }
  // Grid + axes.
  const float step = (y1 - y0) > 0.3f ? 0.1f : 0.05f;
  for (float v = std::ceil(y0 / step) * step; v <= y1 + 1e-4f; v += step) {
    c->drawLine(plot.fLeft, Y(v), plot.fRight, Y(v), Stroke(kGrid, 1));
    DrawText(c, Pct(v, 0), plot.fLeft - 6 * s_, Y(v) + 4 * s_, fonts_->Regular(10.5f * s_), kText3,
             Align::kRight);
  }
  // Lead changes and the projection point, derived from the history.
  int prev_leader = -1;
  float called_at = -1;
  float last_label_x = -1e9f;
  for (size_t i : idx) {
    const auto& v = h->votes[i];
    const int leader = static_cast<int>(std::max_element(v.begin(), v.end()) - v.begin());
    if (prev_leader >= 0 && leader != prev_leader && progress(i) > 0.05f) {
      const float lx = X(h->minutes[i]);
      const bool label = lx - last_label_x > 44 * s_;
      if (label) last_label_x = lx;
      c->drawLine(lx, plot.fTop, lx, plot.fBottom,
                  Dashed(SkColorSetRGB(224, 142, 11), 1.2f * s_, 4 * s_, 4 * s_, 0.8f));
      if (label) {
        DrawText(c, L_.T("ev.lead_change.title"), lx + 3 * s_, plot.fTop + 11 * s_,
                 fonts_->Bold(10.5f * s_), SkColorSetRGB(224, 142, 11));
      }
    }
    prev_leader = leader;
    if (race->is_smd() && called_at < 0 && progress(i) >= 0.25f) {
      std::vector<int64_t> s = v;
      std::sort(s.rbegin(), s.rend());
      int64_t total = 0;
      for (int64_t x : v) total += x;
      const double remaining = total * (1.0 / progress(i) - 1.0) * 1.15;
      if (s.size() > 1 && static_cast<double>(s[0] - s[1]) > remaining) called_at = h->minutes[i];
    }
  }
  if (called_at >= 0) {
    const float cx = X(called_at);
    c->drawLine(cx, plot.fTop, cx, plot.fBottom, Stroke(kRed, 1.5f * s_, 0.8f));
    DrawText(c, L_.T("ev.called.title"), cx + 3 * s_, plot.fTop + 24 * s_, fonts_->Bold(10.5f * s_), kRed);
  }
  c->save();
  c->clipRect(plot.makeOutset(8 * s_, 8 * s_));
  // Series: revealed left-to-right on open, the live end extends smoothly.
  const float reveal = Ease(chart_age_ / 0.9f);
  const double last_min = h->minutes[idx.back()];
  const float shown_last = Approach(anim_, "trend_end:" + race->id, static_cast<float>(last_min), 5);
  const double clip_min = std::min<double>(shown_last, h->minutes[idx.front()] +
                                                        (last_min - h->minutes[idx.front()]) * reveal);
  struct EndLabel {
    float y;
    int cand;
    double value;
  };
  std::vector<EndLabel> ends;
  for (int cnd : cands) {
    const uint32_t color = m.data->party(race->candidates[cnd].party).color;
    SkPathBuilder b;
    bool started = false;
    float ex = 0, ey = 0;
    double ev = 0;
    for (size_t k = 0; k < idx.size(); ++k) {
      const size_t i = idx[k];
      double minute = h->minutes[i];
      double value = share(i, cnd);
      if (minute > clip_min) {
        if (k == 0) break;
        const size_t p = idx[k - 1];
        const double t = (clip_min - h->minutes[p]) / std::max(1e-6, minute - h->minutes[p]);
        value = share(p, cnd) + (value - share(p, cnd)) * t;
        minute = clip_min;
      }
      const float px = X(minute), py = Y(value);
      if (!started) {
        b.moveTo(px, py);
        started = true;
      } else {
        b.lineTo(px, py);
      }
      ex = px, ey = py, ev = value;
      if (minute >= clip_min) break;
    }
    if (!started) continue;
    c->drawPath(b.detach(), Stroke(color, (cnd == cands[0] ? 3.2f : 2.4f) * s_));
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 4);
    c->drawCircle(ex, ey, (6 + 4 * pulse) * s_, P(color, 0.25f));
    c->drawCircle(ex, ey, 4.5f * s_, P(color));
    ends.push_back({ey, cnd, ev});
  }
  c->restore();
  // End labels in a column right of the plot, pushed apart and kept inside.
  std::sort(ends.begin(), ends.end(), [](const EndLabel& a, const EndLabel& b) { return a.y < b.y; });
  const float label_h = race->is_pr() ? 24 * s_ : 30 * s_;
  float prev_y = -1e9f;
  for (EndLabel& e : ends) {
    e.y = std::max(e.y, prev_y + label_h);
    prev_y = e.y;
  }
  const float overflow = ends.empty() ? 0 : ends.back().y - (plot.fBottom + 6 * s_);
  if (overflow > 0) {
    for (EndLabel& e : ends) e.y -= overflow;
  }
  for (const EndLabel& e : ends) {
    const uint32_t color = m.data->party(race->candidates[e.cand].party).color;
    const float lx = plot.fRight + 12 * s_;
    const float av = race->is_pr() ? 20 * s_ : 24 * s_;
    DrawEntrant(c, m, *race, e.cand, lx, e.y - av / 2, av);
    const std::string key = "trend_v:" + race->id + std::to_string(e.cand);
    const double shown = Roll(key, e.value * 10000) / 10000;
    if (race->is_pr()) {
      const float tx = lx + av + 5 * s_;
      const float nw = DrawText(c, Ellipsize(fonts_->Bold(12 * s_), EntrantName(*race, e.cand, m), 70 * s_),
                                tx, e.y + 4 * s_, fonts_->Bold(12 * s_), kText);
      DrawText(c, Pct(shown, 1), tx + nw + 6 * s_, e.y + 4 * s_, fonts_->Bold(11.5f * s_), color | 0xFF000000);
    } else {
      DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), EntrantName(*race, e.cand, m), 100 * s_),
               lx + av + 5 * s_, e.y - 1 * s_, fonts_->Bold(12.5f * s_), kText);
      DrawText(c, Pct(shown, 2), lx + av + 5 * s_, e.y + 13 * s_, fonts_->Bold(12 * s_), color | 0xFF000000);
    }
  }
}

void Dashboard::DrawSeatProjection(SkCanvas* c, const DashboardModel& m, SkRect area, float x0,
                                   float x1) {
  const auto& info = m.data->info();
  const int majority = info.majority > 0 ? info.majority : 233;
  const int super = info.supermajority > 0 ? info.supermajority : 310;
  c->drawLine(area.fLeft, area.fTop, area.fRight, area.fTop, Stroke(kGrid, 1));
  DrawText(c, L_.T("trend.seats_title"), area.fLeft, area.fTop + 22 * s_, fonts_->Bold(14 * s_), kText);
  const SkRect plot = SkRect::MakeLTRB(x0, area.fTop + 34 * s_, x1, area.fBottom - 20 * s_);
  const std::vector<SeatSample>* hist = m.seat_history;
  if (!hist || hist->empty() || plot.height() < 40 * s_) {
    DrawText(c, L_.T("chart.nodata"), plot.centerX(), plot.centerY(), fonts_->Regular(14 * s_), kText2,
             Align::kCenter);
    return;
  }
  auto seats_of = [](const SeatSample& s, const std::string& party) {
    for (const auto& [code, n] : s.seats) {
      if (code == party) return n;
    }
    return 0;
  };
  // Parties to plot: the largest in the latest sample.
  std::vector<std::pair<std::string, int>> top;
  for (const auto& [code, n] : hist->back().seats) {
    if (n > 0) top.push_back({code, n});
  }
  std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  if (top.size() > 6) top.resize(6);
  int peak = 0;
  for (const SeatSample& s : *hist) {
    for (const auto& [code, n] : s.seats) peak = std::max(peak, n);
  }
  const float ymax_target =
      static_cast<float>(std::ceil(std::max(majority * 1.12, peak * 1.08) / 50.0) * 50.0);
  const float ymax = Approach(anim_, "seatproj_max", ymax_target, 4);
  auto X = [&](double minute) {
    return plot.fLeft + static_cast<float>(std::clamp(minute / kNight, 0.0, 1.0)) * plot.width();
  };
  auto Y = [&](double v) { return plot.fBottom - static_cast<float>(v / std::max(1.f, ymax)) * plot.height(); };
  const int step = ymax > 350 ? 100 : 50;
  for (int v = 0; v <= ymax + 0.5f; v += step) {
    c->drawLine(plot.fLeft, Y(v), plot.fRight, Y(v), Stroke(kGrid, 1));
    DrawText(c, std::to_string(v), plot.fLeft - 6 * s_, Y(v) + 4 * s_, fonts_->Regular(10.5f * s_), kText3,
             Align::kRight);
  }
  // Majority (and two-thirds) lines.
  c->drawLine(plot.fLeft, Y(majority), plot.fRight, Y(majority), Dashed(kGold, 1.5f * s_, 5 * s_, 4 * s_, 0.9f));
  DrawText(c, Fmt(L_.T("seats.majority"), {std::to_string(majority)}), plot.fLeft + 6 * s_,
           Y(majority) - 4 * s_, fonts_->Bold(11 * s_), kGold);
  if (super <= ymax) {
    c->drawLine(plot.fLeft, Y(super), plot.fRight, Y(super), Dashed(kText2, 1 * s_, 3 * s_, 4 * s_, 0.6f));
    DrawText(c, Fmt(L_.T("seats.supermajority"), {std::to_string(super)}), plot.fLeft + 6 * s_,
             Y(super) - 4 * s_, fonts_->Regular(10.5f * s_), kText2);
  }
  // Party lines, revealed left to right on open.
  const float reveal = Ease(chart_age_ / 0.9f);
  const double first = hist->front().minute, lastm = hist->back().minute;
  const double clip_min = first + (lastm - first) * reveal;
  struct End {
    float y;
    std::string party;
    int seats;
  };
  std::vector<End> ends;
  c->save();
  c->clipRect(plot.makeOutset(8 * s_, 8 * s_));
  for (const auto& [party, now] : top) {
    const uint32_t color = m.data->party(party).color;
    SkPathBuilder b;
    bool started = false;
    float ex = 0, ey = 0;
    for (size_t k = 0; k < hist->size(); ++k) {
      const SeatSample& s = (*hist)[k];
      double minute = s.minute;
      double value = seats_of(s, party);
      if (minute > clip_min && k > 0) {
        const SeatSample& p = (*hist)[k - 1];
        const double t = (clip_min - p.minute) / std::max(1e-6, minute - p.minute);
        value = seats_of(p, party) + (value - seats_of(p, party)) * t;
        minute = clip_min;
      }
      const float px = X(minute), py = Y(value);
      if (!started) {
        b.moveTo(px, py);
        started = true;
      } else {
        b.lineTo(px, py);
      }
      ex = px, ey = py;
      if (minute >= clip_min) break;
    }
    if (!started) continue;
    c->drawPath(b.detach(), Stroke(color, 2.6f * s_));
    c->drawCircle(ex, ey, 4 * s_, P(color));
    ends.push_back({ey, party, now});
  }
  c->restore();
  std::sort(ends.begin(), ends.end(), [](const End& a, const End& b) { return a.y < b.y; });
  float prev_y = -1e9f;
  for (End& e : ends) {
    e.y = std::max(e.y, prev_y + 17 * s_);
    prev_y = e.y;
  }
  const float overflow = ends.empty() ? 0 : ends.back().y - (plot.fBottom + 4 * s_);
  if (overflow > 0) {
    for (End& e : ends) e.y -= overflow;
  }
  for (const End& e : ends) {
    const election::Party& p = m.data->party(e.party);
    const float lx = plot.fRight + 12 * s_;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, e.y - 5 * s_, 10 * s_, 10 * s_), 2 * s_, 2 * s_),
                 P(p.color));
    const int shown = static_cast<int>(std::lround(Roll("seatproj:" + e.party, e.seats)));
    const float nw = DrawText(c, std::to_string(shown), lx + 16 * s_, e.y + 5 * s_, fonts_->Bold(13 * s_),
                              e.seats >= majority ? kGold : kText);
    DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), L_.PartyShort(p), 80 * s_), lx + 22 * s_ + nw,
             e.y + 5 * s_, fonts_->Regular(11.5f * s_), kText2);
  }
}

// ------------------------------------------------------------- seat arc ----

void Dashboard::DrawSeatArc(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const auto& info = m.data->info();
  const election::SeatSummary* S = m.seats;
  const int total = S && S->total_seats > 0 ? S->total_seats : (info.total_seats > 0 ? info.total_seats : 465);
  const int majority = S && S->majority > 0 ? S->majority : (info.majority > 0 ? info.majority : total / 2 + 1);
  const int super = S && S->supermajority > 0 ? S->supermajority
                                               : (info.supermajority > 0 ? info.supermajority : (total * 2 + 2) / 3);
  DrawText(c, Fmt(L_.T("seats.title"), {std::to_string(total), std::to_string(majority)}), area.fLeft,
           area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);

  // Seats per party: districts (decided / leading) and PR (final bloc /
  // provisional allocation), each tied to its race for clicks.
  struct Seat {
    std::string party;  // "" = undecided / no votes yet
    bool decided = false;
    const Race* race = nullptr;
  };
  struct PartyRows {
    std::vector<const Race*> smd_dec, smd_lead, pr_dec, pr_alloc;
    int smd() const { return static_cast<int>(smd_dec.size() + smd_lead.size()); }
    int pr() const { return static_cast<int>(pr_dec.size() + pr_alloc.size()); }
    int decided() const { return static_cast<int>(smd_dec.size() + pr_dec.size()); }
    int total() const { return smd() + pr(); }
  };
  std::map<std::string, PartyRows> by_party;
  if (S) {
    for (const election::SmdOutcome& o : S->smd) {
      if (!o.race || o.leader < 0 || o.leader >= static_cast<int>(o.race->candidates.size())) continue;
      PartyRows& pr = by_party[o.race->candidates[o.leader].party];
      (o.decided ? pr.smd_dec : pr.smd_lead).push_back(o.race);
    }
    for (const election::PrOutcome& o : S->pr) {
      if (!o.race) continue;
      for (const election::PrListOutcome& l : o.lists) {
        if (l.seats <= 0 || l.list < 0 || l.list >= static_cast<int>(o.race->candidates.size())) continue;
        PartyRows& pr = by_party[o.race->candidates[l.list].party];
        for (int k = 0; k < l.seats; ++k) (o.final ? pr.pr_dec : pr.pr_alloc).push_back(o.race);
      }
    }
  }
  std::vector<std::string> order;
  for (const auto& [code, rows] : by_party) {
    if (rows.total() > 0) order.push_back(code);
  }
  std::sort(order.begin(), order.end(), [](const std::string& a, const std::string& b) {
    return SeatOrder(a) != SeatOrder(b) ? SeatOrder(a) < SeatOrder(b) : a < b;
  });
  std::vector<Seat> seats;
  int assigned = 0;
  for (const std::string& code : order) assigned += by_party[code].total();
  const int undecided = std::max(0, total - assigned);
  bool grey_done = false;
  auto add_grey = [&] {
    for (int k = 0; k < undecided; ++k) seats.push_back({});
    grey_done = true;
  };
  for (const std::string& code : order) {
    if (!grey_done && SeatOrder(code) > kUndecidedOrder) add_grey();
    const PartyRows& rows = by_party[code];
    for (const Race* r : rows.smd_dec) seats.push_back({code, true, r});
    for (const Race* r : rows.pr_dec) seats.push_back({code, true, r});
    for (const Race* r : rows.smd_lead) seats.push_back({code, false, r});
    for (const Race* r : rows.pr_alloc) seats.push_back({code, false, r});
  }
  if (!grey_done) add_grey();
  if (static_cast<int>(seats.size()) > total) seats.resize(total);

  // Legend under the arc (measured first so the arc can use the rest).
  const SkFont num_f = fonts_->Bold(22 * s_);
  const SkFont name_f = fonts_->Bold(12 * s_);
  const SkFont sub_f = fonts_->Regular(10.5f * s_);
  std::vector<std::string> legend = order;
  std::sort(legend.begin(), legend.end(), [&](const std::string& a, const std::string& b) {
    return by_party[a].total() != by_party[b].total() ? by_party[a].total() > by_party[b].total() : a < b;
  });
  struct Item {
    std::string party;
    float w;
  };
  std::vector<std::vector<Item>> lines(1);
  float line_w = 0;
  for (const std::string& code : legend) {
    const PartyRows& rows = by_party[code];
    const std::string sub = Fmt(L_.T("live.seats_breakdown"), {std::to_string(rows.smd()), std::to_string(rows.pr())});
    const float w = 16 * s_ + TextWidth(num_f, "000") + 6 * s_ +
                    std::max(TextWidth(name_f, L_.PartyShort(m.data->party(code))), TextWidth(sub_f, sub)) + 18 * s_;
    if (line_w + w > area.width() && !lines.back().empty()) {
      lines.emplace_back();
      line_w = 0;
    }
    lines.back().push_back({code, w});
    line_w += w;
  }
  if (lines.size() > 3) lines.resize(3);
  const float line_h = 40 * s_;
  const float legend_h = (legend.empty() ? 0 : lines.size() * line_h) + 8 * s_;

  // Hemicycle geometry: concentric rows, seats per row proportional to the
  // radius; seats sorted by angle so parties form wedges.
  const float cx = area.centerX();
  const float cy = area.fBottom - legend_h - 12 * s_;
  const float R = std::max(40 * s_, std::min(area.width() * 0.46f, cy - area.fTop - 58 * s_));
  const float r0 = R * 0.4f;
  const int nrows = total > 300 ? 12 : total > 100 ? 8 : 4;
  std::vector<float> radii(nrows);
  float rsum = 0;
  for (int i = 0; i < nrows; ++i) {
    radii[i] = r0 + (R - r0) * i / std::max(1, nrows - 1);
    rsum += radii[i];
  }
  std::vector<int> per_row(nrows);
  int placed = 0;
  for (int i = 0; i < nrows; ++i) {
    per_row[i] = static_cast<int>(std::lround(total * radii[i] / rsum));
    placed += per_row[i];
  }
  per_row[nrows - 1] += total - placed;
  struct Pos {
    float angle, x, y;
  };
  std::vector<Pos> pos;
  pos.reserve(total);
  for (int row = 0; row < nrows; ++row) {
    for (int k = 0; k < per_row[row]; ++k) {
      const float a = 3.14159265f * (1 - (k + 0.5f) / per_row[row]);
      pos.push_back({a, cx + std::cos(a) * radii[row], cy - std::sin(a) * radii[row]});
    }
  }
  std::stable_sort(pos.begin(), pos.end(), [](const Pos& a, const Pos& b) { return a.angle > b.angle; });
  const float dot = std::min((R - r0) / std::max(1, nrows - 1) * 0.42f,
                             3.14159265f * r0 / std::max(1, per_row[0]) * 0.42f);
  const float appear_step = 1.2f / std::max(1, total);
  for (int i = 0; i < total && i < static_cast<int>(pos.size()); ++i) {
    const Seat seat = i < static_cast<int>(seats.size()) ? seats[i] : Seat{};
    const uint32_t target = seat.party.empty() ? kEmptySeat : m.data->party(seat.party).color;
    const std::string key = "seat" + std::to_string(i);
    // Colour cross-fade + pop when the seat changes hands.
    auto [last, fresh] = seat_color_.try_emplace(key, target);
    if (!fresh && last->second != target) flash_[key] = 1;
    last->second = target;
    const float rr = Approach(anim_, key + "r", ((target >> 16) & 0xFF) / 255.f, 6);
    const float gg = Approach(anim_, key + "g", ((target >> 8) & 0xFF) / 255.f, 6);
    const float bb = Approach(anim_, key + "b", (target & 0xFF) / 255.f, 6);
    const SkColor col = SkColorSetRGB(static_cast<U8CPU>(rr * 255), static_cast<U8CPU>(gg * 255),
                                      static_cast<U8CPU>(bb * 255));
    const float appear = EaseBack((chart_age_ - i * appear_step) / 0.35f);
    auto fl = flash_.find(key);
    const float flash = fl == flash_.end() ? 0.f : fl->second;
    const float radius = dot * appear * (1 + 0.35f * flash);
    if (radius <= 0.1f) continue;
    if (seat.decided || seat.party.empty()) {
      c->drawCircle(pos[i].x, pos[i].y, radius, P(col, seat.party.empty() ? 0.6f : 1));
    } else if (radius < 6 * s_) {
      c->drawCircle(pos[i].x, pos[i].y, radius, P(col, 0.38f));
    } else {
      c->drawCircle(pos[i].x, pos[i].y, radius, P(col, 0.35f));
      c->drawCircle(pos[i].x, pos[i].y, radius - 1.2f * s_, Stroke(col, 2 * s_));
    }
    if (flash > 0) {
      c->drawCircle(pos[i].x, pos[i].y, radius * (1.2f + 0.8f * (1 - flash)), Stroke(kGold, 1.5f * s_, flash));
    }
    if (seat.decided && radius >= 8 * s_) {
      DrawText(c, "✓", pos[i].x, pos[i].y + radius * 0.35f, fonts_->Bold(radius), SK_ColorWHITE, Align::kCenter);
    }
    if (seat.race) {
      AddHit(SkRect::MakeXYWH(pos[i].x - dot, pos[i].y - dot, dot * 2, dot * 2),
             {DashboardHit::Kind::kRace, ChartKind::kMap, seat.race});
    }
  }
  // Majority marker (the middle seat) and the two-thirds line, counted from
  // the right-hand side of the chamber.
  c->drawLine(cx, cy - R - dot * 2.5f, cx, cy - r0 + dot * 2, Dashed(kGold, 1.5f * s_, 5 * s_, 4 * s_, 0.85f));
  DrawText(c, Fmt(L_.T("seats.majority"), {std::to_string(majority)}), cx, cy - R - dot * 2.5f - 5 * s_,
           fonts_->Bold(12 * s_), kGold, Align::kCenter);
  if (super < total && total - super < static_cast<int>(pos.size())) {
    const float a = (pos[total - super].angle + pos[std::max(0, total - super - 1)].angle) / 2;
    const float ca = std::cos(a), sa = std::sin(a);
    c->drawLine(cx + ca * (r0 - dot * 2), cy - sa * (r0 - dot * 2), cx + ca * (R + dot * 2.5f),
                cy - sa * (R + dot * 2.5f), Dashed(kText2, 1.2f * s_, 4 * s_, 4 * s_, 0.7f));
    DrawText(c, Fmt(L_.T("seats.supermajority"), {std::to_string(super)}), cx + ca * (R + dot * 3.5f),
             cy - sa * (R + dot * 3.5f) - 2 * s_, fonts_->Regular(11 * s_), kText2, Align::kRight);
  }
  // Inside the arc: seats decided so far.
  int decided = 0;
  for (const std::string& code : order) decided += by_party[code].decided();
  const int shown_decided = static_cast<int>(std::lround(Roll("seats:decided", decided)));
  DrawText(c, std::to_string(shown_decided), cx, cy - r0 * 0.38f, fonts_->Bold(30 * s_), kText, Align::kCenter);
  DrawText(c, std::string(L_.T("seats.decided")) + " / " + std::to_string(total), cx, cy - r0 * 0.38f + 18 * s_,
           fonts_->Regular(12 * s_), kText2, Align::kCenter);
  if (!S || assigned == 0) {
    DrawText(c, L_.T("chart.nodata"), cx, cy + 22 * s_, fonts_->Regular(14 * s_), kText3, Align::kCenter);
  } else if (undecided > 0) {
    DrawText(c, Fmt(L_.T("live.seats_undecided"), {std::to_string(undecided)}), cx, cy + 20 * s_,
             fonts_->Regular(11.5f * s_), kText3, Align::kCenter);
  }
  // Solid / faded legend.
  const float ly = area.fTop + 44 * s_;
  c->drawCircle(area.fLeft + 6 * s_, ly - 4 * s_, 6 * s_, P(kText2));
  DrawText(c, L_.T("live.seats_solid"), area.fLeft + 18 * s_, ly, fonts_->Regular(12 * s_), kText2);
  const float lx2 = area.fLeft + 30 * s_ + TextWidth(fonts_->Regular(12 * s_), L_.T("live.seats_solid"));
  c->drawCircle(lx2 + 6 * s_, ly - 4 * s_, 6 * s_, P(kText2, 0.35f));
  c->drawCircle(lx2 + 6 * s_, ly - 4 * s_, 5 * s_, Stroke(kText2, 2 * s_));
  DrawText(c, L_.T("seats.leading"), lx2 + 18 * s_, ly, fonts_->Regular(12 * s_), kText2);
  // Party totals under the arc, big numbers rolling.
  float ty = cy + 38 * s_ + 26 * s_;
  for (const std::vector<Item>& line : lines) {
    float lw = 0;
    for (const Item& it : line) lw += it.w;
    float x = area.centerX() - lw / 2;
    for (const Item& it : line) {
      const PartyRows& rows = by_party[it.party];
      const election::Party& p = m.data->party(it.party);
      const int n = static_cast<int>(std::lround(Roll("seats:" + it.party, rows.total())));
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, ty - 24 * s_, 8 * s_, 30 * s_), 3 * s_, 3 * s_),
                   P(p.color));
      const float nx = x + 14 * s_;
      const float nw = DrawText(c, std::to_string(n), nx, ty, num_f, rows.total() >= majority ? kGold : kText);
      const float tx = nx + std::max(nw, TextWidth(num_f, "00")) + 6 * s_;
      DrawText(c, L_.PartyShort(p), tx, ty - 11 * s_, name_f, kText);
      DrawText(c, Fmt(L_.T("live.seats_breakdown"), {std::to_string(rows.smd()), std::to_string(rows.pr())}), tx,
               ty + 4 * s_, sub_f, kText3);
      x += it.w;
    }
    ty += line_h;
  }
}

// -------------------------------------------------------------- margins ----

void Dashboard::DrawMargins(SkCanvas* c, const DashboardModel& m, SkRect area) {
  DrawText(c, L_.T("margins.title"), area.fLeft, area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);
  DrawText(c, L_.T("margins.hint"), area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12 * s_), kText3);
  std::vector<RaceState> states;
  for (const Race& race : m.data->races()) {
    if (race.is_smd()) states.push_back(StateOf(m, race));
  }
  std::stable_sort(states.begin(), states.end(), [](const RaceState& a, const RaceState& b) {
    if (a.has_votes != b.has_votes) return a.has_votes;
    return a.margin < b.margin;
  });
  const size_t with_votes = static_cast<size_t>(
      std::count_if(states.begin(), states.end(), [](const RaceState& s) { return s.has_votes; }));
  if (with_votes == 0) {
    DrawText(c, L_.T("chart.nodata"), area.centerX(), area.centerY(), fonts_->Regular(16 * s_), kText2,
             Align::kCenter);
    return;
  }
  const float top = area.fTop + 58 * s_;
  const float row_h = 23 * s_;
  const float gap = 18 * s_;
  const int cols = area.width() >= 700 * s_ ? 2 : 1;
  const float col_w = (area.width() - gap * (cols - 1)) / cols;
  const int per_col = std::max(1, static_cast<int>((area.fBottom - top - 16 * s_) / row_h));
  const size_t capacity = static_cast<size_t>(per_col * cols);
  const size_t shown = std::min(with_votes, capacity);
  // Columns inside a cell.
  const float name_w = 86 * s_, who_w = 112 * s_, prog_w = 74 * s_, pct_w = 50 * s_;
  const float bar_w = std::max(20 * s_, col_w - name_w - who_w - prog_w - pct_w - 8 * s_);
  const float scale = 0.30f;  // full bar = 30 points
  for (size_t rank = 0; rank < shown; ++rank) {
    const RaceState& st = states[rank];
    const std::string key = "mrg:" + st.race->id;
    const int col = static_cast<int>(rank) / per_col, row = static_cast<int>(rank) % per_col;
    // Enter staggered, then slide to the current rank.
    const float target_x = area.fLeft + col * (col_w + gap);
    const float target_y = top + row * row_h;
    const float x0 = Approach(anim_, key + "x", target_x, 6);
    float y = Approach(anim_, key + "y", target_y, 6);
    const float appear = Ease((chart_age_ - rank * 0.012f) / 0.35f);
    y += (1 - appear) * 20 * s_;
    c->saveLayerAlphaf(nullptr, appear);
    const float mid = y + row_h / 2;
    if (row % 2 == 0) {
      c->drawRect(SkRect::MakeXYWH(x0 - 4 * s_, y, col_w + 8 * s_, row_h), P(SkColorSetARGB(14, 255, 255, 255)));
    }
    auto fit = flash_.find("row:" + st.race->id);
    const float fl = fit == flash_.end() ? 0.f : fit->second;
    if (fl > 0) {
      c->drawRect(SkRect::MakeXYWH(x0 - 4 * s_, y, col_w + 8 * s_, row_h),
                  P(SkColorSetARGB(static_cast<U8CPU>(80 * fl), 245, 197, 66)));
    }
    DrawText(c, Ellipsize(fonts_->Bold(12 * s_), L_.RaceTitle(*st.race), name_w - 6 * s_), x0, mid + 4.5f * s_,
             fonts_->Bold(12 * s_), kText);
    const Candidate& lead = st.race->candidates[st.leader];
    const election::Party& p = m.data->party(lead.party);
    const float av = std::min(row_h - 5 * s_, 18 * s_);
    avatars_->Draw(c, lead, p, x0 + name_w, mid - av / 2, av);
    DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), L_.CandidateName(lead), who_w - av - 8 * s_),
             x0 + name_w + av + 4 * s_, mid + 4.5f * s_, fonts_->Regular(11.5f * s_), kText2);
    const float bar_x = x0 + name_w + who_w;
    const float w = Approach(anim_, key + "w", std::min(1.f, static_cast<float>(st.margin) / scale) * bar_w, 5);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x, mid - 5 * s_, std::max(3 * s_, w), 10 * s_), 3 * s_,
                                     3 * s_),
                 P(p.color));
    const double shown_margin = Roll(key + "m", st.margin * 10000) / 10000;
    DrawText(c, "+" + Pct(shown_margin, 1), bar_x + std::max(3 * s_, w) + 5 * s_, mid + 4.5f * s_,
             fonts_->Bold(11.5f * s_), kText);
    // Close-race highlight.
    if (st.margin < 0.02 && st.tally->Progress() > 0.3) {
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x - 3 * s_, mid - 8 * s_, 64 * s_, 16 * s_), 5 * s_,
                                       5 * s_),
                   Stroke(SkColorSetRGB(0, 184, 148), 1.5f * s_, 0.6f + 0.4f * std::sin(time_ * 5)));
    }
    // Progress mini-bar + 当確 seal / counted share.
    const float px = x0 + col_w - prog_w;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(px, mid - 3 * s_, 40 * s_, 6 * s_), 3 * s_, 3 * s_), P(kGrid));
    const float pw = Approach(anim_, key + "p", 40 * s_ * static_cast<float>(st.tally->Progress()), 5);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(px, mid - 3 * s_, pw, 6 * s_), 3 * s_, 3 * s_), P(kGold));
    const RaceStatus* rs = StatusOf(m, *st.race);
    if (rs && rs->called >= 0) {
      DrawStamp(c, x0 + col_w - 14 * s_, mid, std::min(row_h * 1.05f, 26 * s_), rs->called_age);
    } else {
      DrawText(c, Pct(st.tally->Progress(), 0), x0 + col_w, mid + 4 * s_, fonts_->Regular(10.5f * s_), kText3,
               Align::kRight);
    }
    c->restore();
    AddHit(SkRect::MakeXYWH(target_x - 4 * s_, target_y, col_w + 8 * s_, row_h),
           {DashboardHit::Kind::kRace, ChartKind::kMap, st.race});
  }
  if (states.size() > shown) {
    DrawText(c, Fmt(L_.T("margins.more"), {std::to_string(states.size() - shown)}), area.fRight,
             area.fBottom - 2 * s_, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  }
}

// -------------------------------------------------------------- parties ----

void Dashboard::DrawParties(SkCanvas* c, const DashboardModel& m, SkRect area) {
  DrawText(c, L_.T("parties.title"), area.fLeft, area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);
  const election::SeatSummary* S = m.seats;
  const int64_t grand = S ? S->pr_votes : 0;
  DrawText(c, Fmt(L_.T("parties.total"), {FormatThousands(static_cast<int64_t>(Roll("parties:total", static_cast<double>(grand))))}),
           area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12.5f * s_), kText3);
  std::vector<const election::PartySeats*> rows;
  if (S) {
    for (const election::PartySeats& p : S->parties) {
      if (p.pr_votes > 0 || p.total() > 0) rows.push_back(&p);
    }
  }
  std::stable_sort(rows.begin(), rows.end(), [](const election::PartySeats* a, const election::PartySeats* b) {
    return a->pr_votes != b->pr_votes ? a->pr_votes > b->pr_votes : a->total() > b->total();
  });
  if (rows.empty() || (grand <= 0 && std::none_of(rows.begin(), rows.end(), [](const auto* p) { return p->total() > 0; }))) {
    DrawText(c, L_.T("chart.nodata"), area.centerX(), area.centerY(), fonts_->Regular(16 * s_), kText2, Align::kCenter);
    return;
  }
  const int majority = S->majority > 0 ? S->majority : 233;
  const float top = area.fTop + 70 * s_;
  const float row_h = std::min(40 * s_, (area.fBottom - top) / std::max<size_t>(1, rows.size()));
  const float label_w = 128 * s_;
  const float seat_col = 50 * s_;
  const float seats_x = area.fRight - seat_col * 3;  // left edge of the seat columns
  const float bar_x = area.fLeft + label_w;
  const float bar_w = std::max(40 * s_, seats_x - bar_x - 104 * s_);
  // Column headers for the seats.
  const char* heads[3] = {"seats.smd", "seats.pr", "seats.sum"};
  for (int k = 0; k < 3; ++k) {
    DrawText(c, L_.T(heads[k]), seats_x + seat_col * (k + 1) - 4 * s_, top - 8 * s_, fonts_->Regular(11 * s_),
             kText3, Align::kRight);
  }
  int64_t max_votes = 1;
  for (const auto* p : rows) max_votes = std::max(max_votes, p->pr_votes);
  const float font_scale = std::clamp(row_h / (40 * s_), 0.75f, 1.f);
  for (size_t rank = 0; rank < rows.size(); ++rank) {
    const election::PartySeats& r = *rows[rank];
    const election::Party& p = m.data->party(r.party);
    const std::string key = "pty:" + r.party;
    const float y = Approach(anim_, key + "y", top + rank * row_h, 6);
    const float appear = Ease((chart_age_ - rank * 0.04f) / 0.4f);
    const float mid = y + row_h / 2;
    c->saveLayerAlphaf(nullptr, appear);
    if (rank % 2 == 0) {
      c->drawRect(SkRect::MakeXYWH(area.fLeft - 4 * s_, y, area.width() + 8 * s_, row_h),
                  P(SkColorSetARGB(12, 255, 255, 255)));
    }
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(area.fLeft, mid - 8 * s_, 6 * s_, 16 * s_), 3 * s_, 3 * s_),
                 P(p.color));
    const SkFont nf = fonts_->Bold(13.5f * s_ * font_scale);
    std::string name = L_.PartyName(p);
    if (TextWidth(nf, name) > label_w - 18 * s_) name = L_.PartyShort(p);
    DrawText(c, Ellipsize(nf, name, label_w - 18 * s_), area.fLeft + 13 * s_, mid + 5 * s_, nf, kText);
    const double shown = Roll(key + "v", static_cast<double>(r.pr_votes));
    const float w = std::max(2.f, static_cast<float>(shown / max_votes) * bar_w * appear);
    const float bh = std::min(row_h * 0.55f, 20 * s_);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x, mid - bh / 2, w, bh), 4 * s_, 4 * s_), P(p.color));
    // Shine on the growing edge while the number rolls.
    if (std::fabs(shown - static_cast<double>(r.pr_votes)) > 1) {
      c->drawRect(SkRect::MakeXYWH(bar_x + w - 6 * s_, mid - bh / 2, 6 * s_, bh), P(SK_ColorWHITE, 0.35f));
    }
    if (r.pr_votes > 0) {
      const std::string votes = FormatThousands(static_cast<int64_t>(shown));
      const float vw = DrawText(c, votes, bar_x + w + 8 * s_, mid + 5 * s_, fonts_->Bold(12.5f * s_ * font_scale), kText);
      DrawText(c, Pct(shown / std::max(1.0, static_cast<double>(grand))), bar_x + w + 14 * s_ + vw, mid + 5 * s_,
               fonts_->Regular(11 * s_ * font_scale), kText3);
    }
    // Seats: district / PR / total (total gold at a majority).
    const int vals[3] = {r.smd, r.pr, r.total()};
    for (int k = 0; k < 3; ++k) {
      const int v = static_cast<int>(std::lround(Roll(key + "s" + std::to_string(k), vals[k])));
      const bool sum = k == 2;
      const SkColor col = sum ? (r.total() >= majority ? kGold : kText) : (v > 0 ? kText2 : kText3);
      DrawText(c, std::to_string(v), seats_x + seat_col * (k + 1) - 4 * s_, mid + 5 * s_,
               sum ? fonts_->Bold(15 * s_ * font_scale) : fonts_->Regular(13 * s_ * font_scale), col, Align::kRight);
    }
    c->restore();
  }
}

// ----------------------------------------------------------------- grid ----

void Dashboard::DrawRaceGrid(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const auto& info = m.data->info();
  int n_smd = 0;
  for (const Race& race : m.data->races()) n_smd += race.is_smd();
  DrawText(c, Fmt(L_.T("grid.title"), {std::to_string(n_smd)}), area.fLeft, area.fTop + 20 * s_,
           fonts_->Bold(20 * s_), kText);
  DrawText(c, L_.T("grid.legend"), area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12 * s_), kText3);
  // Districts per bloc, prefecture by prefecture.
  struct BlocCol {
    const election::Bloc* bloc;
    std::vector<std::vector<const Race*>> prefs;
    int count = 0;
  };
  std::vector<BlocCol> blocs;
  for (const election::Bloc& b : info.blocs) {
    BlocCol col{&b, {}, 0};
    for (const std::string& pref : b.prefs) {
      std::vector<const Race*> list;
      for (const Race& race : m.data->races()) {
        if (race.is_smd() && race.pref == pref) list.push_back(&race);
      }
      col.count += static_cast<int>(list.size());
      if (!list.empty()) col.prefs.push_back(std::move(list));
    }
    if (col.count > 0) blocs.push_back(std::move(col));
  }
  if (blocs.empty()) return;
  // Layout: blocs as columns in one or two bands; the largest tile that
  // fits wins (prefectures start a new row when that still fits).
  const float top = area.fTop + 54 * s_;
  const float head_h = 32 * s_;
  const float g = 3 * s_;
  const float col_gap = 10 * s_;
  const float band_gap = 12 * s_;
  struct Fit {
    int bands = 1, per_row = 1;
    float tile = 0, col_w = 0;
    bool pref_rows = false;
  } best;
  auto rows_of = [&](const BlocCol& b, int per_row, bool pref_rows) {
    if (!pref_rows) return (b.count + per_row - 1) / per_row;
    int rows = 0;
    for (const auto& list : b.prefs) rows += (static_cast<int>(list.size()) + per_row - 1) / per_row;
    return rows;
  };
  const int nb = static_cast<int>(blocs.size());
  for (int bands = 1; bands <= 2; ++bands) {
    const int cols = (nb + bands - 1) / bands;
    const float col_w = (area.width() - col_gap * (cols - 1)) / cols;
    for (bool pref_rows : {true, false}) {
      for (float t = 40 * s_; t >= 10 * s_; t -= 1 * s_) {
        if (t <= best.tile) break;
        const int per_row = std::max(1, static_cast<int>((col_w + g) / (t + g)));
        float height = 0;
        for (int band = 0; band < bands; ++band) {
          int tallest = 0;
          for (int k = band * cols; k < std::min(nb, (band + 1) * cols); ++k) {
            tallest = std::max(tallest, rows_of(blocs[k], per_row, pref_rows));
          }
          height += head_h + tallest * (t + g) + (band > 0 ? band_gap : 0);
        }
        if (height <= area.fBottom - top) {
          best = {bands, per_row, t, col_w, pref_rows};
          break;
        }
      }
    }
  }
  if (best.tile <= 0) best = {2, 1, 10 * s_, (area.width() - col_gap * 5) / 6, false};
  const int cols = (nb + best.bands - 1) / best.bands;
  const float t = best.tile;
  const SkFont num_f = fonts_->Bold(std::min(t * 0.5f, 13 * s_));
  const Race* hovered = nullptr;
  SkRect hovered_rect = SkRect::MakeEmpty();
  int tile_index = 0;
  float band_top = top;
  for (int band = 0; band < best.bands; ++band) {
    int tallest = 0;
    for (int k = band * cols; k < std::min(nb, (band + 1) * cols); ++k) {
      const BlocCol& b = blocs[k];
      tallest = std::max(tallest, rows_of(b, best.per_row, best.pref_rows));
      const float x0 = area.fLeft + (k - band * cols) * (best.col_w + col_gap);
      // Header: bloc name, and the leading parties in its districts.
      DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), ShortBloc(L_.BlocName(*b.bloc)), best.col_w), x0,
               band_top + 13 * s_, fonts_->Bold(12.5f * s_), kText);
      std::map<std::string, int> leaders;
      int row = 0, colk = 0;
      for (const auto& list : b.prefs) {
        for (const Race* race : list) {
          const RaceState st = StateOf(m, *race);
          const RaceStatus* rs = StatusOf(m, *race);
          const election::SmdOutcome* so = m.seats ? m.seats->Smd(race) : nullptr;
          int leader = st.leader;
          if (rs && rs->called >= 0) leader = rs->called;
          const bool called = rs && rs->called >= 0;
          const bool decided = called || (so && so->decided);
          if (leader >= 0) leaders[race->candidates[leader].party]++;
          const float tx = x0 + colk * (t + g);
          const float ty = band_top + head_h + row * (t + g);
          const SkRect tile = SkRect::MakeXYWH(tx, ty, t, t);
          const float appear = EaseBack((chart_age_ - tile_index * 0.003f) / 0.3f);
          ++tile_index;
          c->save();
          c->translate(tile.centerX(), tile.centerY());
          c->scale(std::max(0.01f, appear), std::max(0.01f, appear));
          c->translate(-tile.centerX(), -tile.centerY());
          const float rad = std::min(4 * s_, t * 0.2f);
          uint32_t color = kEmptySeat;
          float alpha = 0.85f;
          if (leader >= 0) {
            color = m.data->party(race->candidates[leader].party).color;
            alpha = decided ? 1.f : 0.42f;
          }
          c->drawRRect(SkRRect::MakeRectXY(tile, rad, rad), P(color, alpha));
          if (leader >= 0 && !decided) {
            c->drawRRect(SkRRect::MakeRectXY(tile.makeInset(0.75f * s_, 0.75f * s_), rad, rad),
                         Stroke(color, 1.5f * s_, 0.9f));
          }
          if (t >= 15 * s_) {
            const uint32_t ink = decided ? InkOn(color) : 0xFFFFFFFF;
            DrawText(c, DistrictNumber(*race), tile.centerX(), tile.centerY() + num_f.getSize() * 0.36f, num_f,
                     SkColorSetA(ink, decided ? 255 : 210), Align::kCenter);
          }
          if (called) {  // 当確 marker: white corner notch
            SkPathBuilder tri;
            tri.moveTo(tile.fRight - t * 0.38f, tile.fTop);
            tri.lineTo(tile.fRight, tile.fTop);
            tri.lineTo(tile.fRight, tile.fTop + t * 0.38f);
            tri.close();
            c->save();
            c->clipRRect(SkRRect::MakeRectXY(tile, rad, rad), true);
            c->drawPath(tri.detach(), P(SK_ColorWHITE, 0.95f));
            c->restore();
          }
          auto fit = flash_.find("row:" + race->id);
          const float fl = fit == flash_.end() ? 0.f : fit->second;
          if (fl > 0) c->drawRRect(SkRRect::MakeRectXY(tile, rad, rad), Stroke(kGold, (1 + 2 * fl) * s_, fl));
          c->restore();
          if (tile.contains(m.mouse_x, m.mouse_y)) {
            hovered = race;
            hovered_rect = tile;
          }
          AddHit(tile.makeOutset(g / 2, g / 2), {DashboardHit::Kind::kRace, ChartKind::kMap, race});
          if (++colk >= best.per_row) {
            colk = 0;
            ++row;
          }
        }
        if (best.pref_rows && colk > 0) {
          colk = 0;
          ++row;
        }
      }
      // Leading parties of the bloc under its name.
      std::vector<std::pair<std::string, int>> lp(leaders.begin(), leaders.end());
      std::sort(lp.begin(), lp.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
      std::string summary;
      for (size_t i = 0; i < lp.size() && i < 3; ++i) {
        summary += (i ? " " : "") + L_.PartyShort(m.data->party(lp[i].first)) + std::to_string(lp[i].second);
      }
      if (summary.empty()) summary = Fmt(L_.T("grid.bloc_count"), {std::to_string(b.count)});
      DrawText(c, Ellipsize(fonts_->Regular(10 * s_), summary, best.col_w), x0, band_top + 26 * s_,
               fonts_->Regular(10 * s_), kText3);
    }
    band_top += head_h + tallest * (t + g) + band_gap;
  }
  // Hover card for the district under the cursor.
  if (hovered) {
    const RaceState st = StateOf(m, *hovered);
    const RaceStatus* rs = StatusOf(m, *hovered);
    std::string line1 = L_.RaceTitle(*hovered);
    std::string line2 = L_.T("chart.nodata");
    std::string line3;
    int leader = st.leader;
    if (rs && rs->called >= 0) leader = rs->called;
    if (leader >= 0) {
      const Candidate& cand = hovered->candidates[leader];
      line2 = L_.CandidateName(cand) + " · " + L_.PartyShort(m.data->party(cand.party));
      if (rs && rs->called >= 0) line2 += std::string("  ") + L_.T("stamp.called");
    }
    if (st.has_votes) {
      line3 = Fmt(L_.T("grid.margin"), {FormatThousands(st.margin_votes)}) + " · " +
              Fmt(L_.T("callout.counted"), {Pct(st.tally->Progress(), 0)});
    }
    const SkFont f1 = fonts_->Bold(13 * s_), f2 = fonts_->Regular(12 * s_);
    const float w = std::max({TextWidth(f1, line1), TextWidth(f2, line2), TextWidth(f2, line3)}) + 20 * s_;
    const float h = (line3.empty() ? 44 : 60) * s_;
    float bx = std::clamp(hovered_rect.centerX() - w / 2, area.fLeft, area.fRight - w);
    float by = hovered_rect.fTop - h - 6 * s_;
    if (by < area.fTop) by = hovered_rect.fBottom + 6 * s_;
    const SkRect card = SkRect::MakeXYWH(bx, by, w, h);
    c->drawRRect(SkRRect::MakeRectXY(card, 8 * s_, 8 * s_), P(SkColorSetARGB(245, 20, 34, 52)));
    c->drawRRect(SkRRect::MakeRectXY(card, 8 * s_, 8 * s_), Stroke(SkColorSetARGB(90, 255, 255, 255), 1));
    c->drawRRect(SkRRect::MakeRectXY(hovered_rect.makeOutset(1.5f * s_, 1.5f * s_), 4 * s_, 4 * s_),
                 Stroke(SK_ColorWHITE, 1.5f * s_));
    DrawText(c, line1, bx + 10 * s_, by + 18 * s_, f1, kText);
    DrawText(c, line2, bx + 10 * s_, by + 36 * s_, f2, kText2);
    if (!line3.empty()) DrawText(c, line3, bx + 10 * s_, by + 52 * s_, f2, kText3);
  }
}

// ------------------------------------------------------------------ PiP ----

void Dashboard::DrawPip(SkCanvas* c, const DashboardModel& m) {
  pip_rect_ = SkRect::MakeEmpty();
  if (!m.pip) return;
  // Queue management: breaking items interrupt, otherwise rotate.
  pip_age_ += dt_;
  pip_swap_ = std::min(1.f, pip_swap_ + dt_ / 0.45f);
  const bool breaking_waiting = !pip_queue_.empty() && pip_queue_.front().breaking;
  const float hold = 6.5f;
  if (!pip_has_current_ || pip_age_ > hold || (breaking_waiting && pip_age_ > 2.5f && !pip_current_.breaking) ||
      (breaking_waiting && pip_age_ > 4.f)) {
    PipItem next;
    bool have = false;
    if (!pip_queue_.empty()) {
      next = pip_queue_.front();
      pip_queue_.pop_front();
      have = true;
    } else if (pip_has_current_ && pip_recent_.size() > 1 && pip_age_ > hold) {
      next = pip_recent_.front();  // rotate the recent items
      pip_recent_.pop_front();
      have = true;
    }
    if (have) {
      if (pip_has_current_) {
        pip_previous_ = pip_current_;
        pip_recent_.push_back(pip_current_);
        while (pip_recent_.size() > 8) pip_recent_.pop_front();
        pip_swap_ = 0;
      }
      pip_current_ = next;
      pip_has_current_ = true;
      pip_age_ = 0;
    } else if (pip_has_current_ && pip_age_ > hold) {
      pip_age_ = hold - 2;  // nothing new: keep showing
    }
  }
  if (!pip_has_current_) return;

  const float panel_x = Layout(m.width, m.height).panel_x;
  const float w = 340 * s_, h = 178 * s_;
  // Bottom-right of the map normally; bottom-left (over the feed) while a
  // chart is open so it never covers the chart.
  const bool chart_open = m.chart != ChartKind::kMap;
  const float tx = chart_open ? 16 * s_ : panel_x - 16 * s_ - w;
  const float ty = chart_open ? m.height - 146 * s_ - h : m.height - 186 * s_ - h;
  const SkRect win = SkRect::MakeXYWH(Approach(anim_, "pip_x", tx, 7), Approach(anim_, "pip_y", ty, 7), w, h);
  pip_rect_ = win;
  // Soft shadow + window.
  c->drawRRect(SkRRect::MakeRectXY(win.makeOffset(0, 4 * s_).makeOutset(3 * s_, 3 * s_), 14 * s_, 14 * s_),
               P(SK_ColorBLACK, 0.35f));
  c->drawRRect(SkRRect::MakeRectXY(win, 12 * s_, 12 * s_), P(SkColorSetARGB(245, 12, 22, 36)));
  c->drawRRect(SkRRect::MakeRectXY(win.makeInset(0.5f, 0.5f), 12 * s_, 12 * s_),
               Stroke(pip_current_.breaking ? kRed : SkColorSetARGB(70, 255, 255, 255), 1.5f * s_));
  // Header.
  const float hh = 26 * s_;
  c->save();
  c->clipRRect(SkRRect::MakeRectXY(win, 12 * s_, 12 * s_), true);
  c->drawRect(SkRect::MakeXYWH(win.fLeft, win.fTop, w, hh), P(SkColorSetARGB(255, 20, 34, 52)));
  c->drawCircle(win.fLeft + 13 * s_, win.fTop + hh / 2, 4.5f * s_, P(kRed, 0.55f + 0.45f * std::sin(time_ * 5)));
  const std::string live = L_.T(m.simulated ? (m.replay ? "live.replay_tag" : "live.sim_tag") : "live.live_tag");
  const float lw = DrawText(c, live, win.fLeft + 22 * s_, win.fTop + 17.5f * s_, fonts_->Bold(11 * s_), kRed);
  DrawText(c, L_.T("pip.title"), win.fLeft + 30 * s_ + lw, win.fTop + 17.5f * s_, fonts_->Bold(12 * s_), kText);
  DrawText(c, "×", win.fRight - 12 * s_, win.fTop + 18 * s_, fonts_->Bold(15 * s_), kText2, Align::kCenter);
  AddHit(win, {DashboardHit::Kind::kPip, ChartKind::kMap, pip_current_.event.race});
  AddHit(SkRect::MakeXYWH(win.fRight - 24 * s_, win.fTop, 24 * s_, hh), {DashboardHit::Kind::kPipClose});

  auto draw_item = [&](const PipItem& it, float dx, float alpha) {
    c->save();
    c->translate(dx, 0);
    c->saveLayerAlphaf(nullptr, alpha);
    const float x = win.fLeft + 12 * s_;
    float y = win.fTop + hh + 20 * s_;
    const float tw = w - 24 * s_;
    if (!it.is_news && it.event.race) {
      const election::Race& race = *it.event.race;
      float tx2 = x;
      if (it.breaking) {
        const std::string tag = L_.T("breaking");
        const float cw = TextWidth(fonts_->Bold(10.5f * s_), tag) + 8 * s_;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(tx2, y - 11 * s_, cw, 15 * s_), 3 * s_, 3 * s_), P(kRed));
        DrawText(c, tag, tx2 + 4 * s_, y, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
        tx2 += cw + 6 * s_;
      }
      std::string title = std::string(L_.T(std::string("ev.") + election::EventKey(it.event.type) + ".title")) +
                          " · " + it.event.time_label;
      if (it.event.type == election::EventType::kCalled && it.event.zero_call) {
        title += std::string(" · ") + L_.T("chip.zero");
      }
      DrawText(c, Ellipsize(fonts_->Bold(11.5f * s_), title, x + tw - tx2), tx2, y, fonts_->Bold(11.5f * s_), kGold);
      y += 22 * s_;
      const std::vector<std::string> lines = WrapText(fonts_->Bold(15 * s_), EventText(it.event, m), tw);
      for (size_t i = 0; i < lines.size() && i < 2; ++i) {
        DrawText(c, lines[i], x, y, fonts_->Bold(15 * s_), kText);
        y += 19 * s_;
      }
      // Live mini-bars for the top two of the race (candidates / lists).
      const Tally& t = m.results->RaceTotal(race);
      const std::vector<int> rank = t.Ranking();
      y = std::max(y, win.fBottom - 50 * s_);
      for (size_t k = 0; k < std::min<size_t>(2, rank.size()) && t.TotalVotes() > 0; ++k) {
        if (rank[k] >= static_cast<int>(race.candidates.size())) continue;
        const election::Party& p = m.data->party(race.candidates[rank[k]].party);
        DrawEntrant(c, m, race, rank[k], x, y - 12 * s_, 18 * s_);
        DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), EntrantName(race, rank[k], m), 90 * s_), x + 23 * s_,
                 y + 1 * s_, fonts_->Regular(11.5f * s_), kText2);
        const double share = Roll("pip:" + race.id + std::to_string(rank[k]), t.Share(rank[k]) * 10000) / 10000;
        const float bx = x + 120 * s_, bw = tw - 175 * s_;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bx, y - 5 * s_, bw, 6 * s_), 3 * s_, 3 * s_), P(kGrid));
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bx, y - 5 * s_, bw * static_cast<float>(share), 6 * s_),
                                         3 * s_, 3 * s_),
                     P(p.color));
        DrawText(c, Pct(share), x + tw, y + 1 * s_, fonts_->Bold(11.5f * s_), kText, Align::kRight);
        y += 20 * s_;
      }
    } else if (it.is_news && !it.news.assessments.empty()) {
      const store::Assessment& a = it.news.assessments.front();
      const bool good = a.sentiment == store::Sentiment::kGood, bad = a.sentiment == store::Sentiment::kBad;
      const SkColor col = good ? SkColorSetRGB(46, 204, 113) : bad ? SkColorSetRGB(231, 76, 60) : kText3;
      DrawText(c,
               Ellipsize(fonts_->Bold(11.5f * s_),
                         std::string(L_.T(good ? "news.good" : bad ? "news.bad" : "news.neutral")) + " · " +
                             it.news.article.source,
                         tw),
               x, y, fonts_->Bold(11.5f * s_), col);
      y += 22 * s_;
      std::string title = it.news.digest.empty() ? it.news.article.title : it.news.digest;
      if (it.news.article.simulated && title.rfind("【模擬】", 0) != 0) {
        title = (m.lang == Lang::kEn ? std::string("[") + L_.T("news.mock") + "] "
                                     : std::string("【") + L_.T("news.mock") + "】") +
                title;
      }
      const std::vector<std::string> lines = WrapText(fonts_->Bold(15 * s_), title, tw);
      for (size_t i = 0; i < lines.size() && i < 2; ++i) {
        DrawText(c, lines[i], x, y, fonts_->Bold(15 * s_), kText);
        y += 19 * s_;
      }
      y = std::max(y + 6 * s_, win.fBottom - 36 * s_);
      float cx = x;
      for (const store::Assessment& as : it.news.assessments) {
        const election::Race* race = nullptr;
        const election::Candidate* cand = m.data->CandidateById(as.candidate_id, &race);
        if (!cand || !race) continue;
        const uint32_t cc = as.sentiment == store::Sentiment::kGood  ? 0xFF2ECC71
                            : as.sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                      : 0xFF7F8C8D;
        const std::string label = EntrantName(*race, race->CandidateIndex(cand->id), m) +
                                  (as.sentiment == store::Sentiment::kGood  ? " ▲"
                                   : as.sentiment == store::Sentiment::kBad ? " ▼"
                                                                            : " ●");
        const float cw = TextWidth(fonts_->Bold(11 * s_), label) + 12 * s_;
        if (cx + cw > x + tw) break;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(cx, y - 12 * s_, cw, 17 * s_), 8 * s_, 8 * s_), P(cc));
        DrawText(c, label, cx + 6 * s_, y + 1 * s_, fonts_->Bold(11 * s_), SK_ColorWHITE);
        cx += cw + 5 * s_;
      }
      DrawText(c, it.news.model, x + tw, win.fBottom - 12 * s_, fonts_->Regular(10 * s_), kText3, Align::kRight);
    }
    c->restore();
    c->restore();
  };
  const float swap = Ease(pip_swap_);
  if (swap < 1) draw_item(pip_previous_, -w * swap, 1 - swap);
  draw_item(pip_current_, w * (1 - swap), swap);
  // Dwell progress along the bottom edge.
  c->drawRect(SkRect::MakeXYWH(win.fLeft, win.fBottom - 3 * s_, w * std::min(1.f, pip_age_ / hold), 3 * s_),
              P(pip_current_.breaking ? kRed : kGold, 0.8f));
  c->restore();
}

}  // namespace jpy::ui
