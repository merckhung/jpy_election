// Election-night behaviour of the app: turning results updates into events,
// seat projections, map effects, news items and database records; pinning
// the home region.

#include <algorithm>
#include <cmath>

#include "glm/common.hpp"
#include "src/app/app.h"

namespace jpy::app {
namespace {

constexpr float kPi = 3.14159265f;

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

uint32_t Lighten(uint32_t argb, float t) {
  auto ch = [&](int shift) {
    const float v = ((argb >> shift) & 0xFF) * (1 - t) + 255 * t;
    return static_cast<uint32_t>(v) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

}  // namespace

std::vector<int> App::VisibleRegions() const {
  return tree_.region(DisplayParent()).children;
}

bool App::ProjectRegion(int region, float* x, float* y) const {
  if (region < 0 || region >= tree_.size()) return false;
  auto it = heights_.find(region);
  const float h = it == heights_.end() ? 0.f : it->second;
  glm::vec2 s;
  if (!camera_.WorldToScreen(glm::vec3(tree_.region(region).label, h), &s)) return false;
  *x = s.x;
  *y = s.y;
  return true;
}

int App::RaceAnchor(const election::Race& race) const {
  if (race.is_smd()) return tree_.FindByCode(race.region);
  // PR bloc: the focused prefecture when it belongs to the bloc.
  const geo::Region& f = tree_.region(focus_);
  if (!f.county_code.empty() && race.CoversPref(f.county_code)) {
    return tree_.FindByCode(f.county_code);
  }
  return -1;
}

void App::RecomputeSeats() {
  seats_ = election::ComputeSeats(data_, *results_, [this](const election::Race& race) {
    return tracker_.CalledCandidate(race.id);
  });
}

void App::SampleHistory(const election::ResultsView& view, double minute) {
  if (minute < 0) return;
  auto& results = const_cast<election::ResultsView&>(view);
  for (const election::Race& race : data_.races()) {
    const election::Tally& t = results.RaceTotal(race);
    ui::RaceHistory& h = history_[race.id];
    // Rewound: drop samples from the "future".
    while (!h.minutes.empty() && h.minutes.back() > minute + 1e-3) {
      h.minutes.pop_back();
      h.votes.pop_back();
      h.progress.pop_back();
    }
    if (!h.minutes.empty() && minute - h.minutes.back() < 0.99) {
      h.votes.back() = t.votes;  // same minute: refresh
      h.progress.back() = static_cast<float>(t.Progress());
      continue;
    }
    h.minutes.push_back(static_cast<float>(minute));
    h.votes.push_back(t.votes);
    h.progress.push_back(static_cast<float>(t.Progress()));
  }
}

void App::SampleSeats(double minute) {
  if (minute < 0) return;
  while (!seat_history_.empty() && seat_history_.back().minute > minute + 1e-3) {
    seat_history_.pop_back();  // rewound
  }
  ui::SeatSample sample;
  sample.minute = static_cast<float>(minute);
  for (const election::PartySeats& p : seats_.parties) sample.seats.push_back({p.party, p.total()});
  if (!seat_history_.empty() && minute - seat_history_.back().minute < 0.99) {
    seat_history_.back() = std::move(sample);  // same minute: refresh
  } else {
    seat_history_.push_back(std::move(sample));
  }
}

void App::SetChart(ui::ChartKind kind) {
  chart_ = kind;
  if (kind == ui::ChartKind::kTrend) {
    // Trend: the focused district's race (or the bloc in PR mode), else the
    // closest district on the board.
    const election::Race* focused = mode_ == ui::ColorMode::kPr
                                        ? results_->PrRaceForRegion(focus_)
                                        : results_->SmdRaceForRegion(focus_);
    if (focused) {
      chart_race_ = focused;
    } else if (!chart_race_) {
      double best = 2;
      for (const election::Race& race : data_.races()) {
        if (!race.is_smd()) continue;
        const election::Tally& t = results_->RaceTotal(race);
        if (t.TotalVotes() <= 0) continue;
        const auto rank = t.Ranking();
        const double margin = rank.size() > 1 ? t.Share(rank[0]) - t.Share(rank[1]) : 1;
        if (margin < best) best = margin, chart_race_ = &race;
      }
      if (!chart_race_) chart_race_ = &data_.races().front();
    }
  }
}

void App::CycleChartRace(int step) {
  const auto& races = data_.races();
  int i = 0;
  for (size_t k = 0; k < races.size(); ++k) {
    if (&races[k] == chart_race_) i = static_cast<int>(k);
  }
  i = (i + step + static_cast<int>(races.size())) % static_cast<int>(races.size());
  chart_race_ = &races[i];
}

bool App::HandleOverlayClick() {
  const ui::DashboardHit hit = dashboard_->HitTest(mouse_.x, mouse_.y);
  using Kind = ui::DashboardHit::Kind;
  switch (hit.kind) {
    case Kind::kChartTab:
      SetChart(hit.chart);
      return true;
    case Kind::kPipClose:
      pip_ = false;
      return true;
    case Kind::kRace:
    case Kind::kPip:
      if (hit.race) {
        int target = hit.race->is_smd() ? tree_.FindByCode(hit.race->region) : -1;
        if (target < 0 && !hit.race->prefs.empty()) target = tree_.FindByCode(hit.race->prefs.front());
        if (hit.kind == Kind::kRace && chart_ == ui::ChartKind::kTrend) {
          chart_race_ = hit.race;
        } else if (target >= 0) {
          SetChart(ui::ChartKind::kMap);  // back to the primary view, on that race
          if (hit.race->is_pr()) mode_ = ui::ColorMode::kPr;
          SetFocus(target, true);
        }
      }
      return true;
    default:
      return dashboard_->Captures(mouse_.x, mouse_.y);
  }
}

void App::OnResults(double now) {
  const double minute =
      sim_ ? sim_->clock_minutes() : election::MinutesAfterClose(snapshot_->updated_at);
  SampleHistory(*results_, minute);
  RecomputeSeats();  // calls known so far (PR eligibility of called candidates)
  const election::EventBatch batch = tracker_.Update(*results_, data_.races(), &seats_);
  RecomputeSeats();  // with this update's calls
  SampleSeats(minute);
  RebuildRaceStatus(0);
  if (batch.baseline) return;
  ui::DashboardModel m;
  m.data = &data_;
  m.tree = &tree_;
  m.results = results_.get();
  m.seats = &seats_;
  m.lang = lang_;
  m.focus = focus_;
  m.visible_regions = VisibleRegions();
  dashboard_->PushBatch(batch, minute, m);
  SpawnEventEffects(batch);
  SubmitEventNews(batch);
  RecordToDatabase(batch);
  (void)now;
}

void App::RebuildRaceStatus(float dt) {
  for (auto& [id, st] : race_status_) st.called_age += dt;
  if (dt > 0) return;
  for (const election::Race& race : data_.races()) {
    ui::RaceStatus& st = race_status_[race.id];
    st.declared = st.conceded = -1;
    for (const election::Declaration& d : snapshot_->declarations) {
      if (d.race_id != race.id) continue;
      const int who = race.CandidateIndex(d.candidate_id);
      if (d.type == election::Declaration::Type::kVictory) st.declared = who;
      else if (d.type == election::Declaration::Type::kConcede) st.conceded = who;
    }
    int called = -1;
    if (race.is_smd()) {
      called = tracker_.CalledCandidate(race.id);
      if (called < 0 && tracker_.IsCalled(race.id)) {
        const election::Tally& t = results_->RaceTotal(race);
        if (t.TotalVotes() > 0) called = t.Leader();
      }
    }
    if (called != st.called) {
      st.called = called;
      st.called_age = 0;
    }
  }
}

void App::SpawnEventEffects(const election::EventBatch& batch) {
  const float D = ExtrudeScale();
  const std::vector<int> visible = VisibleRegions();
  auto is_visible = [&](int r) { return std::find(visible.begin(), visible.end(), r) != visible.end(); };

  // New votes: pulse + a beam of light, strongest batches first.
  std::vector<election::RegionDelta> deltas;
  for (const auto& d : batch.deltas) {
    if (is_visible(d.region)) deltas.push_back(d);
  }
  std::sort(deltas.begin(), deltas.end(),
            [](const auto& a, const auto& b) { return a.votes_added > b.votes_added; });
  if (deltas.size() > 40) deltas.resize(40);
  for (const auto& d : deltas) {
    const float mag = std::log10(1.f + static_cast<float>(d.votes_added));
    pulses_[d.region] = std::min(0.8f, pulses_[d.region] + 0.1f + 0.08f * mag);
    uint32_t color = 0xFFBFE3FF;
    if (const election::Race* race = results_->SmdRaceForRegion(d.region)) {
      const election::Tally& t = results_->RaceTally(*race, d.region);
      if (t.TotalVotes() > 0) color = Lighten(data_.party(race->candidates[t.Leader()].party).color, 0.45f);
    }
    MapEffect e;
    e.kind = MapEffect::Kind::kBeam;
    e.region = d.region;
    e.color = color;
    e.life = 1.1f;
    e.size = D * 0.012f * (0.5f + 0.25f * mag);
    effects_.push_back(e);
  }
  // Local lead flips: ripples in the new leader's colour.
  for (const auto& f : batch.flips) {
    if (!is_visible(f.region)) continue;
    pulses_[f.region] = 1.f;
    for (int k = 0; k < 2; ++k) {
      MapEffect e;
      e.region = f.region;
      e.color = data_.party(f.race->candidates[f.leader].party).color;
      e.delay = 0.3f * k;
      e.life = 1.4f;
      e.size = std::max(D * 0.03f, std::sqrt(tree_.region(f.region).area_km2) * 0.8f);
      effects_.push_back(e);
    }
  }
  // Race-level events at the district (its prefecture at the national view,
  // or the view itself when inside the district).
  int called_rings = 0;
  for (const election::ElectionEvent& ev : batch.events) {
    if (ev.leader < 0) continue;
    const int home = RaceAnchor(*ev.race);
    if (home < 0) continue;
    int anchor = -1;
    if (is_visible(home)) {
      anchor = home;
    } else {
      // Nation view: ripple on the district's prefecture.
      const int pref = tree_.AncestorAt(home, geo::Level::kCounty);
      if (pref >= 0 && is_visible(pref)) anchor = pref;
      else if (home == DisplayParent() || tree_.region(focus_).district_code == ev.race->region) {
        anchor = DisplayParent();
      }
    }
    if (anchor < 0) continue;
    // Hundreds of calls land at 20:00 (ゼロ打ち): keep the map readable.
    if (ev.type == election::EventType::kCalled && ++called_rings > 24) continue;
    const uint32_t leader_color = data_.party(ev.race->candidates[ev.leader].party).color;
    int rings = 0;
    uint32_t color = leader_color;
    switch (ev.type) {
      case election::EventType::kLeadChange: rings = 3; break;
      case election::EventType::kVictoryDeclared: rings = 2; color = 0xFFB07CE0; break;
      case election::EventType::kConcession: rings = 1; color = 0xFF9FB0C3; break;
      case election::EventType::kCalled: rings = 2; color = 0xFFF5C542; break;
      case election::EventType::kIncumbentTrailing: rings = 2; color = 0xFFE17055; break;
      case election::EventType::kFinal: rings = 1; color = 0xFF2ECC71; break;
      case election::EventType::kPrSeats: rings = 1; break;
      default: break;
    }
    pulses_[anchor] = 1.f;
    const float size = std::max(D * 0.06f, std::sqrt(tree_.region(anchor).area_km2) * 1.1f);
    for (int k = 0; k < rings; ++k) {
      MapEffect e;
      e.region = anchor;
      e.color = color;
      e.delay = 0.28f * k;
      e.life = 1.8f;
      e.size = size;
      effects_.push_back(e);
    }
    if (ev.type == election::EventType::kCalled || ev.type == election::EventType::kVictoryDeclared) {
      MapEffect e;
      e.kind = MapEffect::Kind::kBeam;
      e.region = anchor;
      e.color = color;
      e.life = 2.4f;
      e.size = D * 0.05f;
      effects_.push_back(e);
    }
  }
  if (effects_.size() > 400) effects_.erase(effects_.begin(), effects_.end() - 400);
}

void App::SubmitEventNews(const election::EventBatch& batch) {
  if (!news_) return;
  using election::EventType;
  using store::Sentiment;
  const ui::Localizer ja(ui::Lang::kJa);
  for (const election::ElectionEvent& ev : batch.events) {
    if (ev.type == EventType::kFirstReturns || ev.leader < 0) continue;
    if (ev.type == EventType::kLeadChange && ev.progress < 0.08) continue;
    const election::Race& race = *ev.race;
    if (!race.is_smd()) continue;  // assessments are about candidates
    auto id = [&](int i) { return i >= 0 ? race.candidates[i].id : std::string(); };
    auto name = [&](int i) { return i >= 0 ? race.candidates[i].name_ja : std::string("?"); };
    const std::string key = ev.type == EventType::kCalled && ev.zero_call
                                ? std::string("ev.zero_call.text")
                                : std::string("ev.") + election::EventKey(ev.type) + ".text";
    const std::string title = ui::Fmt(ui::Tr(ui::Lang::kJa, key),
                                      {ja.RaceTitle(race), name(ev.leader), name(ev.previous),
                                       std::to_string(ev.margin)});
    std::vector<store::Assessment> as;
    switch (ev.type) {
      case EventType::kLeadChange:
        as = {{id(ev.leader), Sentiment::kGood, "逆転リード"}, {id(ev.previous), Sentiment::kBad, "逆転される"}};
        break;
      case EventType::kVictoryDeclared:
        as = {{id(ev.leader), Sentiment::kGood, "勝利宣言"}};
        break;
      case EventType::kConcession:
        as = {{id(ev.leader), Sentiment::kBad, "敗北を認める"}};
        if (ev.previous >= 0 && ev.previous != ev.leader) {
          as.push_back({id(ev.previous), Sentiment::kGood, "対立候補が敗北を認める"});
        }
        break;
      case EventType::kCalled:
        as = {{id(ev.leader), Sentiment::kGood, "当選確実"}};
        break;
      case EventType::kIncumbentTrailing:
        as = {{id(ev.previous), Sentiment::kBad, "前職が劣勢"}, {id(ev.leader), Sentiment::kGood, "前職をリード"}};
        break;
      case EventType::kCloseRace:
        as = {{id(ev.leader), Sentiment::kNeutral, "大接戦"}, {id(ev.previous), Sentiment::kNeutral, "大接戦"}};
        break;
      case EventType::kFinal:
        as = {{id(ev.leader), Sentiment::kGood, "開票終了・当選"}};
        break;
      default:
        break;
    }
    if (as.empty()) continue;
    const std::string url = std::string(snapshot_->simulated ? "sim://" : "event://") + race.id +
                            "/" + election::EventKey(ev.type) + "/" + ev.time_label + "/" +
                            id(ev.leader);
    news_->Submit(news::ArticleFromEvent(title, url, snapshot_->updated_at, as, snapshot_->simulated));
  }
}

void App::RecordToDatabase(const election::EventBatch& batch) {
  if (!db_.is_open()) return;
  const bool sim = snapshot_->simulated;
  for (const election::ElectionEvent& ev : batch.events) {
    const election::Race& race = *ev.race;
    db_.RecordEvent(snapshot_->updated_at, election::EventKey(ev.type), race.id,
                    ev.leader >= 0 ? race.candidates[ev.leader].id : "",
                    ev.previous >= 0 ? race.candidates[ev.previous].id : "", ev.margin, ev.progress,
                    sim);
  }
  // Race totals at most once per snapshot timestamp (minute resolution).
  if (snapshot_->updated_at == last_recorded_time_ || batch.votes_added == 0) return;
  last_recorded_time_ = snapshot_->updated_at;
  std::vector<store::Database::RaceTotal> totals;
  for (const election::Race& race : data_.races()) {
    const election::Tally& t = results_->RaceTotal(race);
    for (size_t i = 0; i < race.candidates.size(); ++i) {
      totals.push_back({race.id, race.candidates[i].id, i < t.votes.size() ? t.votes[i] : 0,
                        t.units_counted, t.units_total});
    }
  }
  db_.RecordTotals(snapshot_->updated_at, sim, totals);
}

void App::UpdateEffects(float dt) {
  for (MapEffect& e : effects_) e.age += dt;
  effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                [](const MapEffect& e) { return e.age > e.delay + e.life; }),
                 effects_.end());
  for (auto it = pulses_.begin(); it != pulses_.end();) {
    it->second -= dt * 1.4f;
    if (it->second <= 0) it = pulses_.erase(it);
    else ++it;
  }
  RebuildRaceStatus(dt);
}

void App::BuildEffects(render::FrameInput* in, ui::DashboardModel* m, float D) {
  for (const MapEffect& e : effects_) {
    const float t = (e.age - e.delay) / e.life;
    if (t < 0 || t > 1) continue;
    const geo::Region& r = tree_.region(e.region);
    const float h = heights_.count(e.region) ? heights_[e.region] : 0.f;
    if (e.kind == MapEffect::Kind::kRing) {
      ui::RingEffect ring;
      ring.color = e.color;
      ring.alpha = (1 - t) * 0.95f;
      ring.width = 3.f * (1 - t) + 1.f;
      const float radius = e.size * (0.15f + 0.85f * Ease(t));
      bool ok = true;
      for (int k = 0; k <= 48 && ok; ++k) {
        const float a = 2 * kPi * k / 48;
        glm::vec2 s;
        ok = camera_.WorldToScreen(
            glm::vec3(r.label + glm::vec2(std::cos(a), std::sin(a)) * radius, h + 0.02f), &s);
        ring.points.push_back({s.x, s.y});
      }
      if (ok) m->rings.push_back(std::move(ring));
    } else {
      render::BarInstance b;
      const float w = std::max(D * 0.0025f, e.size * 0.08f);
      b.base = glm::vec4(r.label, h, w);
      const float a = t < 0.15f ? t / 0.15f : 1 - (t - 0.15f) / 0.85f;
      b.color = glm::vec4(((e.color >> 16) & 0xFF) / 255.f, ((e.color >> 8) & 0xFF) / 255.f,
                          (e.color & 0xFF) / 255.f, 0.75f * a);
      b.size = glm::vec4(e.size * (0.3f + 3.2f * Ease(t * 1.6f)), 0.6f, w, 0.f);
      in->bars.push_back(b);
    }
  }
  // Persistent beacons over the districts of the current view whose winner
  // is projected (too dense to draw nationwide: 289 districts).
  const geo::Region& dp = tree_.region(DisplayParent());
  if (dp.level != geo::Level::kCounty) return;
  for (int district : dp.children) {
    const election::Race* race = results_->SmdRaceForRegion(district);
    if (!race) continue;
    auto it = race_status_.find(race->id);
    if (it == race_status_.end() || it->second.called < 0) continue;
    const geo::Region& r = tree_.region(district);
    const uint32_t c = data_.party(race->candidates[it->second.called].party).color;
    const float pulse = 0.5f + 0.5f * std::sin(it->second.called_age * 2.2f);
    render::BarInstance b;
    const float w = D * 0.004f;
    b.base = glm::vec4(r.label + glm::vec2(0, -D * 0.012f), heights_[district], w);
    b.color = glm::vec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f,
                        0.35f + 0.25f * pulse);
    b.size = glm::vec4(D * (0.06f + 0.01f * pulse) * Ease(it->second.called_age / 1.2f), 0.8f, w, 0);
    in->bars.push_back(b);
  }
}

void App::GenerateMockNews(double now) {
  if (!mock_news_ || !news_) return;
  // Simulated clock when simulating; otherwise one mock minute per real second
  // (minutes relative to the 20:00 close).
  const double minute = sim_ ? sim_->clock_minutes() : now - 180;
  if (minute < mock_minute_) mock_minute_ = minute;  // rewound
  if (minute - mock_minute_ < 1) return;
  for (auto& m : mock_news_->Between(mock_minute_, minute, opt_.mock_news_per_hour)) {
    news_->SubmitArticle(std::move(m.article));
  }
  mock_minute_ = minute;
}

void App::RefreshNews(double now) {
  if (!db_.is_open() || now < next_news_refresh_) return;
  next_news_refresh_ = now + 0.5;
  news_view_.enabled = news_ != nullptr;
  news_view_.status = news_ ? news_->Status() : "";
  news_view_.total = db_.TotalCounts();
  news_view_.by_candidate = db_.CountsByCandidate();
  news_view_.latest = db_.LatestNews(12);
  // Animate articles classified since the last refresh (not the initial load).
  int64_t newest = last_news_id_;
  for (auto it = news_view_.latest.rbegin(); it != news_view_.latest.rend(); ++it) {
    if (it->article.id <= last_news_id_) continue;
    newest = std::max(newest, it->article.id);
    if (last_news_id_ < 0 || it->assessments.empty()) continue;
    ui::DashboardModel m;
    m.data = &data_;
    m.tree = &tree_;
    m.lang = lang_;
    m.focus = focus_;
    m.visible_regions = VisibleRegions();
    dashboard_->PushNews(*it, m);
    // Map: a soft ripple on the subject's district (or its prefecture at the
    // national view) in good/bad colour.
    const election::Race* race = nullptr;
    if (data_.CandidateById(it->assessments.front().candidate_id, &race) && race) {
      const int home = RaceAnchor(*race);
      const std::vector<int> visible = VisibleRegions();
      auto is_visible = [&](int r) {
        return r >= 0 && std::find(visible.begin(), visible.end(), r) != visible.end();
      };
      int anchor = -1;
      if (is_visible(home)) anchor = home;
      else if (home >= 0 && is_visible(tree_.AncestorAt(home, geo::Level::kCounty))) {
        anchor = tree_.AncestorAt(home, geo::Level::kCounty);
      } else if (tree_.region(focus_).district_code == race->region) {
        anchor = DisplayParent();
      }
      if (anchor >= 0) {
        MapEffect e;
        e.region = anchor;
        e.color = it->assessments.front().sentiment == store::Sentiment::kGood  ? 0xFF2ECC71
                  : it->assessments.front().sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                                : 0xFFB0BEC5;
        e.life = 1.6f;
        e.size = std::max(ExtrudeScale() * 0.04f, std::sqrt(tree_.region(anchor).area_km2) * 0.9f);
        effects_.push_back(e);
      }
    }
  }
  last_news_id_ = std::max<int64_t>(newest, 0);
}

void App::TogglePin() {
  if (!db_.is_open()) return;
  if (pinned_ == focus_) {
    db_.DeleteSetting("home_region");
    pinned_ = -1;
    dashboard_->Notify(ui::Tr(lang_, "pin.unpinned"));
  } else {
    db_.SetSetting("home_region", tree_.region(focus_).code);
    pinned_ = focus_;
    dashboard_->Notify(std::string(ui::Tr(lang_, "pin.pinned")) + " · " +
                       ui::Localizer(lang_).RegionName(tree_.region(focus_)));
  }
}

void App::SeekSimulation(double minutes) {
  if (!sim_) return;
  const bool backwards = minutes < sim_->clock_minutes();
  sim_->SeekClock(minutes);
  if (backwards) {
    // Rewinding: forget event state so nothing fires twice.
    tracker_.Reset();
    dashboard_->ResetLive();
    effects_.clear();
  }
}

void App::RestartSimulation() {
  if (!sim_) return;
  sim_->SeekClock(0);
  sim_->set_paused(false);
  tracker_.Reset();
  dashboard_->ResetLive();
  dashboard_->SeedInflow({});
  effects_.clear();
  pulses_.clear();
  shown_color_.clear();
  bar_anim_.clear();
  history_.clear();
  seat_history_.clear();
  race_status_.clear();
  last_recorded_time_.clear();
  mock_minute_ = -180;
  last_news_id_ = -1;
  db_.ClearSimulated();
  dashboard_->Notify(ui::Tr(lang_, "simulation.restarted"));
}

}  // namespace jpy::app
