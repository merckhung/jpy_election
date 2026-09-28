// Election-night "live layer" of the dashboard: breaking-news banners, map
// call-outs, the event feed, the counting timeline, rolling numbers and the
// small animations that make incoming data visible.
//
// Animation vocabulary (see README "Election night"):
//   * new votes      rolling counters, "+N" floaters, row flashes; on the map
//                    a pulse (height bump + glow) and a light beam per region
//   * lead change    red 速報 banner, call-out on the district, cards slide
//                    into the new order with a gold flash, map colour
//                    cross-fades with two ripples in the new leader's colour
//   * victory speech banner + call-out; 万歳 chip on the card (purple)
//   * concession     banner + call-out; card dims, 敗戦の弁 chip
//   * 当選確実       red 当確 seal slams onto the card, gold beacon and
//                    ripples over the district. Calls come in bursts (at
//                    20:00 sharp dozens of ゼロ打ち calls, then all night), so
//                    they are coalesced into counted "当選確実 12人" banners
//                    listing the parties and a few names, rate-limited
//   * PR seats       a bloc's D'Hondt seats move between lists: feed line,
//                    call-out when the bloc's prefecture is in view
//   * steady inflow  every ~2.5 s a call-out on the busiest visible area
//                    shows who leads whom and by how much

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "src/election/results_source.h"
#include "src/store/db.h"
#include "src/ui/dashboard.h"

namespace jpy::ui {

using election::ElectionEvent;
using election::EventType;

namespace {

constexpr SkColor kPanel = SkColorSetARGB(232, 10, 20, 33);
constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kBreaking = SkColorSetRGB(214, 48, 49);
constexpr SkColor kGold = SkColorSetRGB(245, 197, 66);

// Banners for 当確 calls: gather calls for a moment, then at most one banner
// every few seconds.
constexpr float kCalledGather = 0.8f;
constexpr float kCalledCooldown = 3.5f;
constexpr float kToastLife = 5.5f;

SkPaint FillA(SkColor c, float alpha) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  p.setAlphaf(std::clamp(alpha, 0.f, 1.f) * SkColorGetA(c) / 255.f);
  return p;
}

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

// Alpha envelope: fade in over `in`, hold, fade out over `out`.
float Envelope(float age, float life, float in = 0.25f, float out = 0.5f) {
  if (age < in) return age / in;
  if (age > life - out) return std::max(0.f, (life - age) / out);
  return 1;
}

uint32_t EventColor(EventType t) {
  switch (t) {
    case EventType::kLeadChange: return 0xFFE08E0B;
    case EventType::kVictoryDeclared: return 0xFF9B59B6;
    case EventType::kConcession: return 0xFF7F8C8D;
    case EventType::kCalled: return 0xFFD63031;
    case EventType::kIncumbentTrailing: return 0xFFE17055;
    case EventType::kCloseRace: return 0xFF00B894;
    case EventType::kFinal: return 0xFF2E9E5B;
    case EventType::kPrSeats: return 0xFF1ABC9C;
    default: return 0xFF3A6EA5;
  }
}

// First UTF-8 code point of `s` (or up to `ascii_max` leading ASCII chars).
std::string Initial(const std::string& s, size_t ascii_max) {
  if (s.empty()) return "?";
  const unsigned char c = s[0];
  if (c < 0x80) {
    size_t n = 0;
    while (n < s.size() && n < ascii_max && static_cast<unsigned char>(s[n]) < 0x80 &&
           s[n] != ' ' && s[n] != '.') {
      ++n;
    }
    return s.substr(0, std::max<size_t>(1, n));
  }
  const size_t n = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
  return s.substr(0, std::min(n, s.size()));
}

bool Visible(const DashboardModel& m, int region) {
  return region >= 0 &&
         std::find(m.visible_regions.begin(), m.visible_regions.end(), region) !=
             m.visible_regions.end();
}

// "【模擬】" / "[MOCK] " before simulated headlines (unless already there).
std::string MarkMock(const Localizer& L, std::string title) {
  if (title.rfind("【", 0) == 0 && title.find(L.T("news.mock")) != std::string::npos) return title;
  if (title.rfind("【模擬】", 0) == 0) return title;
  if (L.lang() == Lang::kEn) return std::string("[") + L.T("news.mock") + "] " + title;
  return std::string("【") + L.T("news.mock") + "】" + title;
}

std::string EventTitleKey(const ElectionEvent& e) {
  return std::string("ev.") + election::EventKey(e.type) + ".title";
}

}  // namespace

double Dashboard::Roll(const std::string& key, double target, double* added) {
  auto [it, inserted] = nums_.try_emplace(key, AnimNum{target, target});
  AnimNum& n = it->second;
  if (added) *added = inserted ? 0 : std::max(0.0, target - n.target);
  if (target < n.target) n.shown = target;  // clock moved backwards: snap
  n.target = target;
  const double k = 1 - std::exp(-dt_ * 3.5);
  n.shown += (n.target - n.shown) * k;
  if (std::fabs(n.target - n.shown) < 1) n.shown = n.target;
  return n.shown;
}

float Dashboard::Approach(std::unordered_map<std::string, float>& map, const std::string& key,
                          float target, float rate) {
  auto [it, inserted] = map.try_emplace(key, target);
  float& v = it->second;
  v += (target - v) * (1 - std::exp(-dt_ * rate));
  return v;
}

const RaceStatus* Dashboard::StatusOf(const DashboardModel& m, const election::Race& race) const {
  if (!m.race_status) return nullptr;
  auto it = m.race_status->find(race.id);
  return it == m.race_status->end() ? nullptr : &it->second;
}

std::string Dashboard::EntrantName(const election::Race& race, int i,
                                   const DashboardModel& m) const {
  if (i < 0 || i >= static_cast<int>(race.candidates.size())) return "?";
  const election::Candidate& cand = race.candidates[i];
  if (race.is_pr()) return L_.PartyShort(m.data->party(cand.party));
  return L_.CandidateName(cand);
}

void Dashboard::DrawEntrant(SkCanvas* c, const DashboardModel& m, const election::Race& race,
                            int i, float x, float y, float size) {
  if (i < 0 || i >= static_cast<int>(race.candidates.size())) return;
  const election::Candidate& cand = race.candidates[i];
  const election::Party& party = m.data->party(cand.party);
  if (race.is_smd()) {
    avatars_->Draw(c, cand, party, x, y, size);
    return;
  }
  // PR list: a party badge (colour disc with the party's initial).
  const float r = size / 2;
  c->drawCircle(x + r, y + r, r, FillA(party.color, 1));
  SkPaint ring = FillA(SK_ColorWHITE, 0.35f);
  ring.setStyle(SkPaint::kStroke_Style);
  ring.setStrokeWidth(std::max(1.f, size * 0.05f));
  c->drawCircle(x + r, y + r, r - ring.getStrokeWidth() / 2, ring);
  const std::string initial = Initial(L_.PartyShort(party), size >= 30 * s_ ? 3 : 2);
  const bool latin = static_cast<unsigned char>(initial[0]) < 0x80;
  const float fs = latin ? size * (initial.size() > 2 ? 0.3f : 0.38f) : size * 0.5f;
  const uint32_t ink = Luminance(party.color) > 0.62f ? 0xFF10161E : 0xFFFFFFFF;
  DrawText(c, initial, x + r, y + r + fs * 0.36f, fonts_->Bold(fs), ink, Align::kCenter);
}

int Dashboard::EventAnchor(const DashboardModel& m, const election::Race& race) const {
  const geo::Region& focus = m.tree->region(m.focus);
  if (race.is_smd()) {
    const int district = m.tree->FindByCode(race.region);
    if (district < 0) return -1;
    if (Visible(m, district)) return district;  // prefecture view: its districts
    const int pref = m.tree->region(district).parent;
    if (Visible(m, pref)) return pref;  // nation view: the prefecture prism
    // Inside the district (its units are shown): the view itself.
    if (m.focus == district || focus.district_code == race.region) return m.focus;
    return -1;
  }
  // PR bloc: only when the focused prefecture (or an area in it) votes in it.
  if (focus.level == geo::Level::kNation) return -1;
  const std::string& pref = focus.level == geo::Level::kCounty ? focus.code : focus.county_code;
  return race.CoversPref(pref) ? m.focus : -1;
}

void Dashboard::ResetLive() {
  pip_queue_.clear();
  toasts_.clear();
  pending_toasts_.clear();
  called_burst_.clear();
  called_burst_age_ = 0;
  called_cooldown_ = 0;
  feed_.clear();
  callouts_.clear();
  floaters_.clear();
  pending_delta_.clear();
  inflow_.clear();
  markers_.clear();
  flash_.clear();
}

void Dashboard::PushBatch(const election::EventBatch& batch, double clock_minutes,
                          const DashboardModel& m) {
  if (clock_minutes >= 0 && batch.votes_added > 0) {
    const size_t bucket = static_cast<size_t>(clock_minutes / 5);
    if (inflow_.size() <= bucket) inflow_.resize(bucket + 1, 0);
    inflow_[bucket] += batch.votes_added;
  }
  for (const auto& d : batch.deltas) pending_delta_[d.region] += d.votes_added;

  // A batch can hold hundreds of events (ゼロ打ち at 20:00, first returns
  // everywhere at once): keep the feed and the map readable.
  const bool crowded = batch.events.size() > 8;
  int callouts_added = 0;
  for (const ElectionEvent& e : batch.events) {
    if (!e.race) continue;
    if (!(crowded && e.type == EventType::kFirstReturns)) {
      feed_.push_front(e);
      if (feed_.size() > 60) feed_.pop_back();
    }
    if (clock_minutes >= 0 && markers_.size() < 4000) {
      markers_.push_back({static_cast<float>(clock_minutes), e.type});
    }
    const std::string key = e.race->id + ":" + std::to_string(e.leader);
    if (e.type == EventType::kLeadChange || e.type == EventType::kCalled ||
        e.type == EventType::kVictoryDeclared) {
      flash_[key] = 1.f;
    }
    flash_["row:" + e.race->id] = 1.f;
    // Early lead changes on a handful of ballots are noise: feed only.
    const bool early_noise = e.type == EventType::kLeadChange && e.progress < 0.08;
    if (e.type != EventType::kFirstReturns && !early_noise) {
      PipItem item;
      item.event = e;
      item.breaking = election::IsBreaking(e.type);
      if (item.breaking) {
        // Breaking items jump ahead of routine ones.
        auto pos = std::find_if(pip_queue_.begin(), pip_queue_.end(),
                                [](const PipItem& p) { return !p.breaking; });
        pip_queue_.insert(pos, item);
      } else {
        pip_queue_.push_back(item);
      }
      while (pip_queue_.size() > 12) pip_queue_.pop_back();
    }
    if (election::IsBreaking(e.type) && !early_noise) {
      if (e.type == EventType::kCalled) {
        if (called_burst_.empty()) called_burst_age_ = 0;
        called_burst_.push_back(e);  // coalesced in UpdateLive
      } else {
        pending_toasts_.push_back(e);
      }
    }

    // Call-out anchored on the district (or its prefecture in the nation
    // view); PR blocs only when their prefecture is in view.
    if (early_noise || e.type == EventType::kFirstReturns || callouts_added >= 4) continue;
    const int anchor = EventAnchor(m, *e.race);
    if (anchor < 0) continue;
    Callout c;
    c.kind = CalloutKind::kEvent;
    c.event = e;
    c.region = anchor;
    c.life = election::IsBreaking(e.type) ? 6.f : 4.5f;
    callouts_.push_back(c);
    ++callouts_added;
  }
  // Local lead flips inside the current view (districts / units).
  for (const auto& f : batch.flips) {
    if (flip_budget_ < 1 || !f.race || !Visible(m, f.region)) continue;
    if (m.tree->region(f.region).level == geo::Level::kCounty) continue;  // covered by events
    flip_budget_ -= 1;
    Callout c;
    c.kind = CalloutKind::kFlip;
    c.event.race = f.race;
    c.event.leader = f.leader;
    c.event.previous = f.previous;
    c.region = f.region;
    c.life = 3.2f;
    callouts_.push_back(c);
  }
  // Keep the map readable: at most 4 call-outs, oldest non-breaking first.
  while (callouts_.size() > 4) {
    auto victim = std::find_if(callouts_.begin(), callouts_.end(), [](const Callout& c) {
      return c.kind != CalloutKind::kEvent || !election::IsBreaking(c.event.type);
    });
    callouts_.erase(victim == callouts_.end() ? callouts_.begin() : victim);
  }
}

void Dashboard::PushNews(const store::ClassifiedArticle& article, const DashboardModel& m) {
  for (const store::Assessment& a : article.assessments) flash_["news:" + a.candidate_id] = 1.f;
  if (article.assessments.empty()) return;
  if (article.model != "event-rule") {
    PipItem item;
    item.is_news = true;
    item.news = article;
    const election::Race* race = nullptr;
    m.data->CandidateById(article.assessments.front().candidate_id, &race);
    item.event.race = race;
    pip_queue_.push_back(item);
    while (pip_queue_.size() > 12) pip_queue_.pop_back();
  }
  // Event-derived items already have their own banner/call-out; ordinary
  // news gets at most one call-out per ~1.5 s and yields to live events.
  if (article.model == "event-rule" || news_budget_ < 1 || callouts_.size() >= 3) return;
  const election::Race* race = nullptr;
  const election::Candidate* cand =
      m.data->CandidateById(article.assessments.front().candidate_id, &race);
  if (!cand || !race) return;
  const int anchor = EventAnchor(m, *race);  // the subject's district
  if (anchor < 0) return;
  news_budget_ -= 1;
  Callout c;
  c.kind = CalloutKind::kNews;
  c.news = article;
  c.event.race = race;
  c.event.leader = race->CandidateIndex(cand->id);
  c.region = anchor;
  c.life = 4.2f;
  callouts_.push_back(c);
  while (callouts_.size() > 6) callouts_.erase(callouts_.begin());
}

void Dashboard::UpdateLive(const DashboardModel& m) {
  dt_ = std::clamp(m.dt, 0.f, 0.25f);
  time_ += dt_;
  flip_budget_ = std::min(2.f, flip_budget_ + dt_ * 0.7f);
  news_budget_ = std::min(2.f, news_budget_ + dt_ * 0.65f);
  for (auto& [k, v] : flash_) v = std::max(0.f, v - dt_ * 0.7f);
  for (Toast& t : toasts_) t.age += dt_;
  while (!toasts_.empty() && toasts_.front().age > kToastLife) toasts_.pop_front();
  // With a chart open, one banner at a time keeps the chart title visible.
  const size_t max_toasts = m.chart == ChartKind::kMap ? 2 : 1;
  auto slot_free = [&] {
    return toasts_.size() < max_toasts && (toasts_.empty() || toasts_.back().age >= 1.2f);
  };
  // 当確 calls: gather a burst, then one banner (the single call, or a
  // counted summary), at most one every few seconds.
  called_cooldown_ = std::max(0.f, called_cooldown_ - dt_);
  if (!called_burst_.empty()) {
    called_burst_age_ += dt_;
    if (called_burst_age_ > kCalledGather && called_cooldown_ <= 0 && slot_free()) {
      Toast t;
      t.event = called_burst_.front();
      if (called_burst_.size() > 1) t.group = std::move(called_burst_);
      toasts_.push_back(std::move(t));
      called_burst_.clear();
      called_burst_age_ = 0;
      called_cooldown_ = kCalledCooldown;
    }
  }
  while (!pending_toasts_.empty() && slot_free()) {
    // Collapse bursts: never queue more than 6 banners.
    while (pending_toasts_.size() > 6) pending_toasts_.pop_front();
    toasts_.push_back({pending_toasts_.front(), 0.f, {}});
    pending_toasts_.pop_front();
  }
  for (Callout& c : callouts_) c.age += dt_;
  callouts_.erase(std::remove_if(callouts_.begin(), callouts_.end(),
                                 [](const Callout& c) { return c.age > c.life; }),
                  callouts_.end());
  for (Floater& f : floaters_) f.age += dt_;
  floaters_.erase(std::remove_if(floaters_.begin(), floaters_.end(),
                                 [](const Floater& f) { return f.age > 1.6f; }),
                  floaters_.end());

  // Periodic "who leads whom" call-out on the busiest visible region.
  lead_timer_ += dt_;
  if (lead_timer_ > 2.5f && !pending_delta_.empty()) {
    lead_timer_ = 0;
    int best = -1;
    int64_t best_votes = 0;
    for (int r : m.visible_regions) {
      auto it = pending_delta_.find(r);
      if (it != pending_delta_.end() && it->second > best_votes) {
        best_votes = it->second;
        best = r;
      }
    }
    // A prefecture spans several districts: take its busiest district.
    int race_region = best;
    if (best >= 0 && m.tree->region(best).level == geo::Level::kCounty) {
      race_region = -1;
      int64_t votes = 0;
      for (int child : m.tree->region(best).children) {
        auto it = pending_delta_.find(child);
        if (it != pending_delta_.end() && it->second > votes) {
          votes = it->second;
          race_region = child;
        }
      }
    }
    pending_delta_.clear();
    const election::Race* race =
        race_region >= 0 ? m.results->SmdRaceForRegion(race_region) : nullptr;
    const bool busy = std::any_of(callouts_.begin(), callouts_.end(),
                                  [&](const Callout& c) { return c.region == best; });
    if (race && !busy) {
      const election::Tally& t = m.results->RaceTally(*race, race_region);
      const std::vector<int> rank = t.Ranking();
      if (t.TotalVotes() > 0 && rank.size() > 1) {
        Callout c;
        c.kind = CalloutKind::kLead;
        c.event.race = race;
        c.event.leader = rank[0];
        c.event.previous = rank[1];
        c.event.margin = t.votes[rank[0]] - t.votes[rank[1]];
        c.event.progress = t.Progress();
        c.region = best;
        c.life = 3.8f;
        callouts_.push_back(c);
      }
    }
  }
}

std::string Dashboard::EventText(const ElectionEvent& e, const DashboardModel& m) const {
  if (!e.race) return "";
  const election::Race& race = *e.race;
  const std::string key = e.type == EventType::kCalled && e.zero_call
                              ? std::string("ev.zero_call.text")
                              : std::string("ev.") + election::EventKey(e.type) + ".text";
  std::string text = Fmt(L_.T(key), {L_.RaceTitle(race), EntrantName(race, e.leader, m),
                                     EntrantName(race, e.previous, m),
                                     FormatThousands(e.margin)});
  if (e.after_declaration) text += L_.T("ev.after_declaration");
  return text;
}

std::string Dashboard::LeadText(const ElectionEvent& e, const DashboardModel& m) const {
  if (!e.race) return "";
  return Fmt(L_.T("lead.text"), {EntrantName(*e.race, e.leader, m),
                                 EntrantName(*e.race, e.previous, m),
                                 FormatThousands(e.margin)});
}

void Dashboard::DrawRings(SkCanvas* c, const DashboardModel& m) {
  for (const RingEffect& r : m.rings) {
    if (r.points.size() < 3) continue;
    SkPathBuilder b;
    b.moveTo(r.points[0]);
    for (size_t i = 1; i < r.points.size(); ++i) b.lineTo(r.points[i]);
    b.close();
    SkPaint p = FillA(r.color, r.alpha);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(r.width * s_);
    c->drawPath(b.detach(), p);
  }
}

void Dashboard::DrawCallouts(SkCanvas* c, const DashboardModel& m) {
  if (!m.project) return;
  const float panel_x = Layout(m.width, m.height).panel_x;
  std::vector<SkRect> placed;
  for (const Callout& co : callouts_) {
    if (!co.event.race) continue;
    float ax, ay;
    if (!m.project(co.region, &ax, &ay)) continue;
    if (ax < 0 || ax > panel_x || ay < 0 || ay > m.height) continue;
    const float a = Envelope(co.age, co.life);
    const election::Race& race = *co.event.race;
    const bool breaking = co.kind == CalloutKind::kEvent && election::IsBreaking(co.event.type);
    std::string title = L_.RegionName(m.tree->region(co.region));
    std::string line, sub;
    uint32_t accent = 0xFF3A6EA5;
    const std::string counted =
        Fmt(L_.T("callout.counted"),
            {std::to_string(static_cast<int>(co.event.progress * 100)) + "%"});
    if (co.kind == CalloutKind::kEvent) {
      line = EventText(co.event, m);
      // Drop the "東京1区：" prefix, the title already names the race.
      const std::string prefix = L_.RaceTitle(race);
      if (line.rfind(prefix, 0) == 0) {
        line = line.substr(prefix.size());
        for (const char* sep : {"：", ": "}) {
          if (line.rfind(sep, 0) == 0) line = line.substr(std::strlen(sep));
        }
      }
      title = std::string(L_.T(EventTitleKey(co.event))) + " · " + L_.RaceTitle(race);
      accent = EventColor(co.event.type);
      sub = co.event.time_label;
      if (co.event.type == EventType::kCalled && co.event.zero_call) {
        sub += std::string("  ") + L_.T("chip.zero");
      } else if (co.event.type != EventType::kPrSeats) {
        sub += "  " + counted;
      }
    } else if (co.kind == CalloutKind::kNews) {
      if (co.news.assessments.empty()) continue;
      const store::Assessment& as = co.news.assessments.front();
      const bool good = as.sentiment == store::Sentiment::kGood;
      const bool bad = as.sentiment == store::Sentiment::kBad;
      title = std::string(L_.T(good ? "news.good" : bad ? "news.bad" : "news.neutral")) + " · " +
              co.news.article.source;
      line = co.news.digest.empty() ? co.news.article.title : co.news.digest;
      if (co.news.article.simulated) line = MarkMock(L_, line);
      sub = as.reason + (as.reason.empty() ? "" : " · ") + co.news.model;
      accent = good ? 0xFF2ECC71 : bad ? 0xFFE74C3C : 0xFF90A4AE;
    } else if (co.kind == CalloutKind::kFlip) {
      line = Fmt(L_.T("flip.text"), {EntrantName(race, co.event.leader, m),
                                    EntrantName(race, co.event.previous, m)});
      accent = 0xFFE08E0B;
    } else {
      line = LeadText(co.event, m);
      sub = L_.RaceTitle(race) + "  " + counted;
      if (co.event.leader >= 0 && co.event.leader < static_cast<int>(race.candidates.size())) {
        accent = m.data->party(race.candidates[co.event.leader].party).color;
      }
    }
    const SkFont tf = fonts_->Bold(12.5f * s_);
    const SkFont lf = fonts_->Bold(14.5f * s_);
    const SkFont sf = fonts_->Regular(11.5f * s_);
    const float av = 26 * s_;
    const float pad = 9 * s_;
    const float text_w = std::max({TextWidth(tf, title), TextWidth(lf, line), TextWidth(sf, sub)});
    const float w = std::min(380 * s_, text_w + av * 2 + pad * 3 + 6 * s_);
    const float h = (sub.empty() ? 50 : 64) * s_;
    // Rise in, float slightly.
    const float lift = (1 - Ease(co.age / 0.35f)) * 12 * s_;
    float bx = std::clamp(ax - w / 2, 8 * s_, panel_x - w - 8 * s_);
    float by = ay - h - 26 * s_ + lift;
    if (by < 8 * s_) by = ay + 26 * s_;
    SkRect box = SkRect::MakeXYWH(bx, by, w, h);
    // Stack away from earlier call-outs: upwards first, then below the anchor.
    auto overlaps = [&](const SkRect& b) {
      for (const SkRect& p : placed) {
        if (SkRect::Intersects(p, b.makeOutset(4 * s_, 4 * s_))) return true;
      }
      return false;
    };
    for (int tries = 0; tries < 6 && overlaps(box); ++tries) box.offset(0, -(h + 8 * s_));
    if (box.fTop < 8 * s_) {
      box.offsetTo(bx, ay + 26 * s_);
      for (int tries = 0; tries < 6 && overlaps(box); ++tries) box.offset(0, h + 8 * s_);
    }
    placed.push_back(box);
    // Pointer.
    SkPathBuilder tri;
    const float px = std::clamp(ax, box.fLeft + 12 * s_, box.fRight - 12 * s_);
    const bool below = box.fTop > ay;
    const float edge = below ? box.fTop : box.fBottom;
    tri.moveTo(px - 7 * s_, edge);
    tri.lineTo(ax, ay);
    tri.lineTo(px + 7 * s_, edge);
    tri.close();
    c->drawPath(tri.detach(), FillA(breaking ? kBreaking : SkColorSetARGB(255, 60, 80, 105), a * 0.9f));
    c->drawCircle(ax, ay, 4 * s_, FillA(accent, a));
    c->drawRRect(SkRRect::MakeRectXY(box, 9 * s_, 9 * s_), FillA(kPanel, a));
    SkPaint border = FillA(breaking ? kBreaking : accent, a);
    border.setStyle(SkPaint::kStroke_Style);
    border.setStrokeWidth((breaking ? 2.f : 1.3f) * s_);
    c->drawRRect(SkRRect::MakeRectXY(box.makeInset(0.5f, 0.5f), 9 * s_, 9 * s_), border);
    c->saveLayerAlphaf(nullptr, a);
    // Portraits / party badges: leader (and the other side, if any).
    float x = box.fLeft + pad;
    const float cy = box.fTop + (h - av) / 2;
    if (co.event.leader >= 0) {
      DrawEntrant(c, m, race, co.event.leader, x, cy, av);
      x += av + 3 * s_;
    }
    if (co.event.previous >= 0) {
      DrawEntrant(c, m, race, co.event.previous, x, cy + av * 0.1f, av * 0.8f);
      x += av * 0.8f + 3 * s_;
    }
    x += 4 * s_;
    const float tw = box.fRight - pad - x;
    float ty = box.fTop + pad + 11 * s_;
    if (breaking) {
      const std::string tag = L_.T("breaking");
      const float cw = TextWidth(fonts_->Bold(10.5f * s_), tag) + 8 * s_;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, ty - 10.5f * s_, cw, 14 * s_), 3 * s_,
                                       3 * s_),
                   FillA(kBreaking, 1));
      DrawText(c, tag, x + 4 * s_, ty, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
      DrawText(c, Ellipsize(tf, title, tw - cw - 6 * s_), x + cw + 6 * s_, ty, tf, kText2);
    } else {
      DrawText(c, Ellipsize(tf, title, tw), x, ty, tf, kText2);
    }
    ty += 19 * s_;
    DrawText(c, Ellipsize(lf, line, tw), x, ty, lf, kText);
    if (!sub.empty()) {
      ty += 16 * s_;
      DrawText(c, Ellipsize(sf, sub, tw), x, ty, sf, kText3);
    }
    c->restore();
  }
}

void Dashboard::DrawToasts(SkCanvas* c, const DashboardModel& m) {
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float left = 590 * s_;
  const float w = std::min(580 * s_, panel_x - left - 16 * s_);
  if (w < 200 * s_) return;
  const float x = left + (panel_x - 16 * s_ - left - w) / 2;
  float y = 16 * s_;
  const bool en = m.lang == Lang::kEn;
  for (const Toast& t : toasts_) {
    const ElectionEvent& e = t.event;
    if (!e.race) continue;
    const bool group = !t.group.empty();
    const float h = (group ? 86 : 66) * s_;
    const float in = Ease(t.age / 0.35f);
    const float out = t.age > kToastLife - 0.45f ? Ease((kToastLife - t.age) / 0.45f) : 1.f;
    const float a = std::min(in, out);
    const float slide = (1 - a) * -24 * s_;
    const election::Race& race = *e.race;
    const SkRect box = SkRect::MakeXYWH(x, y + slide, w, h);
    const uint32_t color = EventColor(e.type);
    c->saveLayerAlphaf(nullptr, a);
    c->drawRRect(SkRRect::MakeRectXY(box, 10 * s_, 10 * s_), FillA(kPanel, 1));
    // Red 速報 slab on the left with a shine sweeping across.
    const SkRect slab = SkRect::MakeXYWH(box.fLeft, box.fTop, 84 * s_, h);
    c->save();
    c->clipRRect(SkRRect::MakeRectXY(box, 10 * s_, 10 * s_), true);
    c->drawRect(slab, FillA(kBreaking, 1));
    const float sweep = std::fmod(t.age * 1.3f, 1.6f) * 1.6f - 0.3f;
    c->drawRect(SkRect::MakeXYWH(slab.fLeft + slab.width() * sweep, slab.fTop, 10 * s_, h),
                FillA(SK_ColorWHITE, 0.18f));
    c->drawRect(SkRect::MakeXYWH(box.fLeft, box.fBottom - 3 * s_,
                                 box.width() * std::clamp(t.age / kToastLife, 0.f, 1.f), 3 * s_),
                FillA(color, 1));
    c->restore();
    DrawText(c, Ellipsize(fonts_->Bold(15 * s_), L_.T("breaking"), slab.width() - 8 * s_),
             slab.centerX(), slab.centerY() + 5 * s_, fonts_->Bold(15 * s_), SK_ColorWHITE,
             Align::kCenter);
    SkPaint border = FillA(kBreaking, 1);
    border.setStyle(SkPaint::kStroke_Style);
    border.setStrokeWidth(1.5f * s_);
    c->drawRRect(SkRRect::MakeRectXY(box.makeInset(0.75f, 0.75f), 10 * s_, 10 * s_), border);
    float ax = slab.fRight + 10 * s_;
    const float av = 42 * s_;
    if (group) {
      // Summary of a burst of 当確 calls: a fan of portraits, the count,
      // the calls by party and the first few names.
      const int n = static_cast<int>(t.group.size());
      const int fan = std::min(n, 3);
      const float step = av * 0.45f;
      for (int k = fan - 1; k >= 0; --k) {
        const ElectionEvent& g = t.group[k];
        if (!g.race) continue;
        DrawEntrant(c, m, *g.race, g.leader, ax + k * step, box.fTop + (h - av) / 2, av);
      }
      ax += av + (fan - 1) * step + 10 * s_;
      const float tw = box.fRight - 12 * s_ - ax;
      bool zero = false;
      std::map<std::string, int> by_party;
      for (const ElectionEvent& g : t.group) {
        zero = zero || g.zero_call;
        if (g.race && g.leader >= 0 && g.leader < static_cast<int>(g.race->candidates.size())) {
          by_party[g.race->candidates[g.leader].party]++;
        }
      }
      std::string title = std::string(L_.T("ev.called.title")) + " · " + e.time_label;
      if (zero) title += std::string(" · ") + L_.T("chip.zero");
      DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), title, tw), ax, box.fTop + 21 * s_,
               fonts_->Bold(12.5f * s_), 0xFF000000 | color);
      // Line 2: "当選確実 12人" + party chips.
      const SkFont big = fonts_->Bold(18 * s_);
      const std::string count = Fmt(L_.T("ev.banner.called"), {std::to_string(n)});
      float cx = ax + DrawText(c, count, ax, box.fTop + 46 * s_, big, kText) + 10 * s_;
      std::vector<std::pair<std::string, int>> parties(by_party.begin(), by_party.end());
      std::sort(parties.begin(), parties.end(), [](const auto& p, const auto& q) {
        return p.second != q.second ? p.second > q.second : p.first < q.first;
      });
      const SkFont pf = fonts_->Bold(12.5f * s_);
      for (const auto& [code, k] : parties) {
        const election::Party& p = m.data->party(code);
        const std::string label = L_.PartyShort(p) + " " + std::to_string(k);
        const float cw = TextWidth(pf, label) + 12 * s_;
        if (cx + cw > box.fRight - 12 * s_) break;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(cx, box.fTop + 32 * s_, cw, 19 * s_),
                                         9.5f * s_, 9.5f * s_),
                     FillA(p.color, 0.9f));
        DrawText(c, label, cx + 6 * s_, box.fTop + 46 * s_, pf,
                 Luminance(p.color) > 0.62f ? 0xFF10161E : SK_ColorWHITE);
        cx += cw + 5 * s_;
      }
      // Line 3: names with party, "… ほか N人".
      std::string names;
      int listed = 0;
      const SkFont nf = fonts_->Regular(12.5f * s_);
      for (const ElectionEvent& g : t.group) {
        if (!g.race || g.leader < 0 || g.leader >= static_cast<int>(g.race->candidates.size())) {
          continue;
        }
        const std::string party = L_.PartyShort(m.data->party(g.race->candidates[g.leader].party));
        const std::string item = EntrantName(*g.race, g.leader, m) +
                                 (g.race->is_pr() ? "" : (en ? " (" + party + ")" : "（" + party + "）"));
        std::string next = names.empty() ? item : names + L_.ListSeparator() + item;
        const std::string more = Fmt(L_.T("live.banner_more"), {std::to_string(n - listed - 1)});
        if (TextWidth(nf, next + " " + more) > tw && listed > 0) break;
        names = next;
        ++listed;
      }
      if (listed < n) names += " " + Fmt(L_.T("live.banner_more"), {std::to_string(n - listed)});
      DrawText(c, Ellipsize(nf, names, tw), ax, box.fTop + 70 * s_, nf, kText2);
    } else {
      if (e.leader >= 0) {
        DrawEntrant(c, m, race, e.leader, ax, box.fTop + (h - av) / 2, av);
        ax += av + 4 * s_;
      }
      if (e.previous >= 0 && e.type != EventType::kConcession) {
        const float av2 = av * 0.72f;
        DrawEntrant(c, m, race, e.previous, ax, box.fTop + (h - av2) / 2, av2);
        ax += av2 + 4 * s_;
      }
      ax += 6 * s_;
      const float tw = box.fRight - 12 * s_ - ax;
      std::string title = std::string(L_.T(EventTitleKey(e))) + " · " + e.time_label;
      if (e.type == EventType::kCalled && e.zero_call) title += std::string(" · ") + L_.T("chip.zero");
      DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), title, tw), ax, box.fTop + 22 * s_,
               fonts_->Bold(12.5f * s_), 0xFF000000 | color);
      DrawText(c, Ellipsize(fonts_->Bold(17 * s_), EventText(e, m), tw), ax, box.fTop + 46 * s_,
               fonts_->Bold(17 * s_), kText);
    }
    c->restore();
    y += h + 8 * s_;
  }
}

void Dashboard::DrawFloaters(SkCanvas* c) {
  for (const Floater& f : floaters_) {
    const float t = f.age / 1.6f;
    const float a = t < 0.15f ? t / 0.15f : 1 - Ease((t - 0.15f) / 0.85f);
    DrawText(c, f.text, f.x, f.y - Ease(t) * 26 * s_, fonts_->Bold(13 * s_),
             SkColorSetA(f.color, static_cast<U8CPU>(255 * std::clamp(a, 0.f, 1.f))),
             Align::kRight);
  }
}

void Dashboard::DrawStamp(SkCanvas* c, float cx, float cy, float size, float age) {
  // Red seal "当確" slamming down: scales from 2.2x with a flash, then rests.
  const float t = std::clamp(age / 0.35f, 0.f, 1.f);
  const float scale = 1 + (1 - Ease(t)) * 1.2f;
  const float alpha = std::min(1.f, 0.2f + t);
  c->save();
  c->translate(cx, cy);
  c->rotate(-14);
  c->scale(scale, scale);
  const float r = size / 2;
  SkPaint ring = FillA(kBreaking, alpha);
  ring.setStyle(SkPaint::kStroke_Style);
  ring.setStrokeWidth(size * 0.07f);
  c->drawCircle(0, 0, r, ring);
  ring.setStrokeWidth(size * 0.025f);
  c->drawCircle(0, 0, r * 0.82f, ring);
  // Fit the word inside the inner ring ("CALLED" is wider than "当確").
  const std::string word = L_.T("live.seal");
  float fs = size * 0.36f;
  const float width = TextWidth(fonts_->Bold(fs), word);
  if (width > size * 0.68f) fs *= size * 0.68f / width;
  DrawText(c, word, 0, fs * 0.36f, fonts_->Bold(fs),
           SkColorSetA(kBreaking, static_cast<U8CPU>(255 * alpha)), Align::kCenter);
  c->restore();
  if (age < 0.6f) {  // impact flash
    c->drawCircle(cx, cy, size * (0.5f + age * 1.5f), FillA(kGold, (0.6f - age) * 0.8f));
  }
}

float Dashboard::DrawFeed(SkCanvas* c, const DashboardModel& m, float x, float y, float w,
                          float max_h) {
  if (feed_.empty() || max_h < 70 * s_) return 0;
  const float pad = 12 * s_;
  const float row = 22 * s_;
  const int rows = std::min<int>(static_cast<int>(feed_.size()),
                                 static_cast<int>((max_h - pad * 2 - 24 * s_) / row));
  if (rows <= 0) return 0;
  const float h = pad * 2 + 24 * s_ + rows * row;
  SkPaint panel = FillA(SkColorSetARGB(222, 10, 20, 33), 1);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, h), 12 * s_, 12 * s_), panel);
  float ty = y + pad + 14 * s_;
  const float title_w = DrawText(c, L_.T("feed.title"), x + pad, ty, fonts_->Bold(15 * s_), kText);
  // Simulated clocks are marked: 模擬 (synthetic) / 再現 (replay).
  if (m.simulated) {
    Chip(c, fonts_, L_.T(m.replay ? "live.replay_tag" : "live.sim_tag"), x + pad + title_w + 8 * s_,
         ty - 13 * s_, 17 * s_, m.replay ? 0xFF3A6EA5 : 0xFFD64545, s_);
  }
  // Blinking live dot.
  c->drawCircle(x + w - pad - 5 * s_, ty - 5 * s_, 4.5f * s_,
                FillA(kBreaking, 0.55f + 0.45f * std::sin(time_ * 5)));
  ty += 8 * s_;
  for (int i = 0; i < rows; ++i) {
    const ElectionEvent& e = feed_[i];
    ty += row;
    const bool breaking = election::IsBreaking(e.type);
    DrawText(c, e.time_label, x + pad, ty, fonts_->Regular(11.5f * s_), kText3);
    const float dot_x = x + pad + 42 * s_;
    c->drawCircle(dot_x, ty - 4.5f * s_, 4 * s_, FillA(EventColor(e.type), 1));
    const SkFont f = breaking ? fonts_->Bold(12.5f * s_) : fonts_->Regular(12.5f * s_);
    DrawText(c, Ellipsize(f, EventText(e, m), w - (dot_x - x) - pad - 8 * s_), dot_x + 9 * s_, ty,
             f, breaking ? kText : kText2);
  }
  return h;
}

void Dashboard::DrawTimeline(SkCanvas* c, const DashboardModel& m) {
  if (m.clock_minutes < 0 && !m.simulated) return;
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float x0 = 16 * s_ + 34 * s_, x1 = panel_x - 130 * s_;
  const float track_y = m.height - 84 * s_;
  const float hist_h = 30 * s_;
  const double span = election::SimulatedResultsSource::kCountMinutes;  // 20:00 -> 04:00
  auto X = [&](double minute) {
    return x0 + static_cast<float>(std::clamp(minute / span, 0.0, 1.0)) * (x1 - x0);
  };
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0 - 44 * s_, track_y - hist_h - 16 * s_,
                                                     panel_x - 16 * s_, track_y + 20 * s_),
                                   10 * s_, 10 * s_),
               FillA(SkColorSetARGB(170, 10, 20, 33), 1));
  // Inflow histogram (district votes per 5 minutes after 20:00).
  int64_t peak = 1;
  for (int64_t v : inflow_) peak = std::max(peak, v);
  const float bw = (x1 - x0) / static_cast<float>(span / 5);
  for (size_t i = 0; i < inflow_.size() && i < static_cast<size_t>(span / 5); ++i) {
    if (!inflow_[i]) continue;
    const float hh = hist_h * static_cast<float>(static_cast<double>(inflow_[i]) / peak);
    c->drawRect(SkRect::MakeXYWH(x0 + i * bw + 0.5f, track_y - 6 * s_ - hh, bw - 1, hh),
                FillA(SkColorSetRGB(88, 160, 230), 0.75f));
  }
  // Track + progress.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0, track_y - 2 * s_, x1, track_y + 2 * s_),
                                   2 * s_, 2 * s_),
               FillA(SkColorSetARGB(70, 255, 255, 255), 1));
  const double now = std::max(0.0, m.clock_minutes);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0, track_y - 2 * s_, X(now), track_y + 2 * s_),
                                   2 * s_, 2 * s_),
               FillA(kGold, 1));
  const int hours = static_cast<int>(span / 60);
  for (int hr = 0; hr <= hours; ++hr) {
    const float tx = X(hr * 60.0);
    c->drawRect(SkRect::MakeXYWH(tx - 0.5f, track_y + 2 * s_, 1, 4 * s_), FillA(kText3, 1));
    // "20:00" ... "00:00" ... "04:00" (wraps past midnight).
    DrawText(c, election::SimulatedResultsSource::ClockLabel(hr * 60.0), tx, track_y + 15 * s_,
             fonts_->Regular(10 * s_), kText3, Align::kCenter);
  }
  // Event markers.
  for (const auto& [minute, type] : markers_) {
    if (type == EventType::kFirstReturns) continue;
    const float mx = X(minute);
    SkPathBuilder b;
    b.moveTo(mx, track_y - 4 * s_);
    b.lineTo(mx - 4 * s_, track_y - 10 * s_);
    b.lineTo(mx + 4 * s_, track_y - 10 * s_);
    b.close();
    c->drawPath(b.detach(), FillA(EventColor(type), 0.95f));
  }
  // Playhead.
  const float px = X(now);
  c->drawCircle(px, track_y, 6 * s_, FillA(SK_ColorWHITE, 1));
  c->drawCircle(px, track_y, 3.5f * s_, FillA(kGold, 1));
  const std::string clock = election::SimulatedResultsSource::ClockLabel(now);
  DrawText(c, clock, x0 - 40 * s_, track_y + 5 * s_, fonts_->Bold(13 * s_), kText);
  if (m.simulated) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "×%g", m.sim_speed);
    std::string speed = buf;
    if (m.sim_paused) speed = std::string(L_.T("timeline.paused")) + " " + speed;
    DrawText(c, speed, panel_x - 26 * s_, track_y + 5 * s_, fonts_->Bold(14 * s_),
             m.sim_paused ? kText3 : kGold, Align::kRight);
    // 模擬 (synthetic night) / 再現 (official results replayed on a clock).
    const std::string tag = L_.T(m.replay ? "live.replay_tag" : "live.sim_tag");
    const float ch = 17 * s_;
    const float cw = ChipWidth(fonts_, tag, ch);
    Chip(c, fonts_, tag, panel_x - 26 * s_ - cw, track_y - hist_h - 6 * s_, ch,
         m.replay ? 0xFF3A6EA5 : 0xFFD64545, s_);
  }
}

void Dashboard::DrawNewsCounts(SkCanvas* c, const DashboardModel& m,
                               const std::string& candidate_id, float x, float y) {
  if (!m.news) return;
  auto it = m.news->by_candidate.find(candidate_id);
  if (it == m.news->by_candidate.end() || it->second.total() == 0) return;
  const store::SentimentCount& n = it->second;
  const SkFont f = fonts_->Bold(11.5f * s_);
  if (auto fl = flash_.find("news:" + candidate_id); fl != flash_.end() && fl->second > 0) {
    const float wbox = 90 * s_;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x - 4 * s_, y - 12 * s_, wbox, 16 * s_), 8 * s_, 8 * s_),
                 FillA(kGold, 0.45f * fl->second));
  }
  x += DrawText(c, "▲" + std::to_string(n.good), x, y, f, SkColorSetRGB(46, 204, 113)) + 6 * s_;
  x += DrawText(c, "▼" + std::to_string(n.bad), x, y, f, SkColorSetRGB(231, 76, 60)) + 6 * s_;
  if (n.neutral) DrawText(c, "●" + std::to_string(n.neutral), x, y, fonts_->Regular(11 * s_), kText3);
}

void Dashboard::DrawNewsPanel(SkCanvas* c, const DashboardModel& m, float x, float y, float w,
                              float h) {
  if (h < 120 * s_) return;
  const float pad = 14 * s_;
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, h), 12 * s_, 12 * s_),
               FillA(SkColorSetARGB(228, 10, 20, 33), 1));
  float ty = y + pad + 16 * s_;
  const float title_w = DrawText(c, L_.T("news.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
  const NewsView* nv = m.news;
  if (!nv || (!nv->enabled && nv->total.total() == 0)) {
    ty += 26 * s_;
    for (const std::string& line :
         WrapText(fonts_->Regular(12.5f * s_), L_.T("news.disabled"), w - 2 * pad)) {
      DrawText(c, line, x + pad, ty, fonts_->Regular(12.5f * s_), kText2);
      ty += 18 * s_;
    }
    return;
  }
  DrawText(c, Ellipsize(fonts_->Regular(10.5f * s_), nv->status, w - 3 * pad - title_w),
           x + w - pad, ty, fonts_->Regular(10.5f * s_), kText3, Align::kRight);
  // Totals: good / neutral / bad bar.
  ty += 20 * s_;
  const store::SentimentCount& t = nv->total;
  DrawText(c,
           Fmt(L_.T("news.totals"), {std::to_string(t.good), std::to_string(t.bad),
                                     std::to_string(t.neutral)}),
           x + pad, ty, fonts_->Regular(12.5f * s_), kText2);
  ty += 8 * s_;
  const SkRect bar = SkRect::MakeXYWH(x + pad, ty, w - 2 * pad, 8 * s_);
  c->drawRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), FillA(SkColorSetARGB(60, 255, 255, 255), 1));
  if (t.total() > 0) {
    c->save();
    c->clipRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), true);
    const float g = bar.width() * t.good / t.total();
    const float n = bar.width() * t.neutral / t.total();
    c->drawRect(SkRect::MakeXYWH(bar.fLeft, bar.fTop, g, bar.height()), FillA(SkColorSetRGB(46, 204, 113), 1));
    c->drawRect(SkRect::MakeXYWH(bar.fLeft + g, bar.fTop, n, bar.height()), FillA(SkColorSetRGB(120, 130, 145), 1));
    c->drawRect(SkRect::MakeLTRB(bar.fLeft + g + n, bar.fTop, bar.fRight, bar.fBottom),
                FillA(SkColorSetRGB(231, 76, 60), 1));
    c->restore();
  }
  ty += 18 * s_;

  // Candidate leaderboard (races in view, or the most-covered nationwide).
  struct Row {
    const election::Race* race;
    int index;
    store::SentimentCount n;
  };
  std::vector<Row> rows;
  const geo::Region& focus = m.tree->region(m.focus);
  const std::string pref = focus.level == geo::Level::kCounty ? focus.code : focus.county_code;
  for (const election::Race& race : m.data->races()) {
    if (focus.level == geo::Level::kCounty && !race.CoversPref(pref)) continue;
    if (focus.level == geo::Level::kTown || focus.level == geo::Level::kVillage) {
      // A district: its SMD race and its bloc's party lists.
      if (race.is_smd() ? race.region != focus.district_code && race.region != focus.code
                        : !race.CoversPref(pref)) {
        continue;
      }
    }
    for (size_t i = 0; i < race.candidates.size(); ++i) {
      auto it = nv->by_candidate.find(race.candidates[i].id);
      if (it != nv->by_candidate.end() && it->second.total() > 0) {
        rows.push_back({&race, static_cast<int>(i), it->second});
      }
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.n.total() != b.n.total() ? a.n.total() > b.n.total()
                                      : a.n.good - a.n.bad > b.n.good - b.n.bad;
  });
  const size_t max_rows = std::min<size_t>(rows.size(), focus.level == geo::Level::kNation ? 6 : 7);
  int peak = 1;
  for (size_t i = 0; i < max_rows; ++i) peak = std::max(peak, std::max(rows[i].n.good, rows[i].n.bad));
  const float row_h = 24 * s_;
  const float mid = x + pad + 150 * s_;
  const float half = (x + w - pad - mid) / 2;
  for (size_t i = 0; i < max_rows; ++i) {
    const Row& row = rows[i];
    ty += row_h;
    DrawEntrant(c, m, *row.race, row.index, x + pad, ty - 15 * s_, 20 * s_);
    DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), EntrantName(*row.race, row.index, m), 110 * s_),
             x + pad + 26 * s_, ty, fonts_->Bold(12.5f * s_), kText);
    // Diverging bars: bad to the left of the axis, good to the right.
    const float gb = half * row.n.good / peak, bb = half * row.n.bad / peak;
    const float axis = mid + half;
    c->drawRect(SkRect::MakeXYWH(axis - bb, ty - 10 * s_, bb, 8 * s_), FillA(SkColorSetRGB(231, 76, 60), 1));
    c->drawRect(SkRect::MakeXYWH(axis, ty - 10 * s_, gb, 8 * s_), FillA(SkColorSetRGB(46, 204, 113), 1));
    c->drawRect(SkRect::MakeXYWH(axis - 0.5f, ty - 13 * s_, 1, 14 * s_), FillA(kText3, 1));
    DrawText(c, std::to_string(row.n.bad), axis - bb - 4 * s_, ty - 1 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetRGB(231, 76, 60), Align::kRight);
    DrawText(c, std::to_string(row.n.good), axis + gb + 4 * s_, ty - 1 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetRGB(46, 204, 113));
  }
  ty += 14 * s_;
  // Latest headlines with per-candidate verdicts.
  for (const store::ClassifiedArticle& a : nv->latest) {
    if (ty + 40 * s_ > y + h - pad) break;
    ty += 20 * s_;
    std::string title = a.digest.empty() ? a.article.title : a.digest;
    if (a.article.simulated) title = MarkMock(L_, title);
    DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), title, w - 2 * pad), x + pad, ty,
             fonts_->Regular(12.5f * s_), kText);
    ty += 17 * s_;
    float cx = x + pad;
    const std::string src = a.article.source + " · " + a.model;
    cx += DrawText(c, Ellipsize(fonts_->Regular(10.5f * s_), src, 120 * s_), cx, ty,
                   fonts_->Regular(10.5f * s_), kText3) + 8 * s_;
    for (const store::Assessment& as : a.assessments) {
      const election::Race* race = nullptr;
      const election::Candidate* cand = m.data->CandidateById(as.candidate_id, &race);
      if (!cand || !race) continue;
      const uint32_t color = as.sentiment == store::Sentiment::kGood ? 0xFF2ECC71
                             : as.sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                       : 0xFF7F8C8D;
      const std::string label =
          EntrantName(*race, race->CandidateIndex(cand->id), m) +
          (as.sentiment == store::Sentiment::kGood  ? " ▲"
           : as.sentiment == store::Sentiment::kBad ? " ▼"
                                                    : " ●");
      const float cw = TextWidth(fonts_->Bold(10.5f * s_), label) + 10 * s_;
      if (cx + cw > x + w - pad) break;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(cx, ty - 11 * s_, cw, 15 * s_), 7 * s_, 7 * s_),
                   FillA(color, 0.85f));
      DrawText(c, label, cx + 5 * s_, ty, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
      cx += cw + 4 * s_;
    }
  }
}

void Dashboard::DrawNotice(SkCanvas* c, const DashboardModel& m) {
  notice_age_ += dt_;
  if (notice_.empty() || notice_age_ > 2.5f) return;
  const float a = Envelope(notice_age_, 2.5f, 0.15f, 0.6f);
  const SkFont f = fonts_->Bold(14 * s_);
  const float w = TextWidth(f, notice_) + 36 * s_;
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float x = (panel_x - w) / 2 + 170 * s_, y = m.height - 150 * s_;
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, 32 * s_), 16 * s_, 16 * s_),
               FillA(SkColorSetARGB(235, 30, 45, 64), a));
  DrawText(c, "★", x + 10 * s_, y + 21 * s_, f, SkColorSetA(kGold, static_cast<U8CPU>(255 * a)));
  DrawText(c, notice_, x + 26 * s_, y + 21 * s_, f, SkColorSetA(kText, static_cast<U8CPU>(255 * a)));
}

}  // namespace jpy::ui
