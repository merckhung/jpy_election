#include "src/ui/i18n.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace jpy::ui {
namespace {

using Entry = std::array<const char*, 3>;  // ja, en, zh-TW

const std::unordered_map<std::string_view, Entry>& Table() {
  static const auto* table = new std::unordered_map<std::string_view, Entry>{
      // --- Core strings (i18n.cc) -------------------------------------------
      {"app.title", {"第51回衆議院議員総選挙", "51st General Election (House of Representatives)", "第51屆日本眾議院選舉"}},
      {"window.title", {"第51回衆議院議員総選挙 2026", "Japan 2026 General Election", "2026 日本眾議院大選"}},
      {"nation.name", {"日本", "Japan", "日本"}},
      {"breaking", {"速報", "BREAKING", "快訊"}},
      {"feed.title", {"開票速報", "Election night feed", "開票快訊"}},
      {"callout.counted", {"開票率 {0}", "{0} counted", "開票 {0}"}},
      {"lead.text", {"{0}が{1}を{2}票リード", "{0} leads {1} by {2}", "{0} 領先 {1} {2} 票"}},
      {"flip.text", {"{0}が{1}を逆転", "{0} overtakes {1}", "{0} 反超 {1}"}},
      {"stamp.elected", {"当選", "WON", "當選"}},
      {"stamp.called", {"当確", "CALLED", "當選確定"}},
      {"chip.declared", {"万歳", "Victory speech", "勝選感言"}},
      {"chip.conceded", {"敗戦の弁", "Conceded", "承認敗選"}},
      {"chip.called", {"当選確実", "Projected", "當選確定"}},
      {"chip.zero", {"ゼロ打ち", "Called at close", "開票即確定"}},
      {"chip.revived", {"比例復活", "Won PR seat", "比例復活"}},
      {"chip.dual", {"重複", "Dual", "重複登記"}},
      {"timeline.paused", {"一時停止", "Paused", "暫停"}},
      // Events: {0} race, {1} leader, {2} previous / other, {3} margin.
      {"ev.after_declaration", {"（勝利宣言後）", " (after declaring victory)", "（曾宣布勝選）"}},
      {"ev.first.title", {"開票開始", "First returns", "開出首票"}},
      {"ev.first.text", {"{0}：開票開始、{1}が先行", "{0}: first returns, {1} ahead", "{0}：開出首批選票，{1} 暫時領先"}},
      {"ev.lead_change.title", {"逆転", "Lead change", "逆轉"}},
      {"ev.lead_change.text", {"{0}：{1}が{2}を逆転", "{0}: {1} overtakes {2}", "{0}：{1} 反超 {2}"}},
      {"ev.victory.title", {"勝利宣言", "Victory speech", "勝選感言"}},
      {"ev.victory.text", {"{0}：{1}が万歳、勝利宣言", "{0}: {1} celebrates victory", "{0}：{1} 發表勝選感言"}},
      {"ev.concede.title", {"敗戦の弁", "Concession", "承認敗選"}},
      {"ev.concede.text", {"{0}：{1}が敗北を認める", "{0}: {1} concedes", "{0}：{1} 承認敗選"}},
      {"ev.called.title", {"当選確実", "Projected winner", "當選確定"}},
      {"ev.called.text", {"{0}：{1}が当選確実", "{0}: {1} projected to win", "{0}：{1} 當選確定"}},
      {"ev.zero_call.text", {"{0}：{1}が当選確実（ゼロ打ち）", "{0}: {1} projected at poll close", "{0}：{1} 投票截止即當選確定"}},
      {"ev.incumbent_trailing.title", {"前職劣勢", "Incumbent trailing", "現任落後"}},
      {"ev.incumbent_trailing.text", {"{0}：前職の{2}が{1}を追う展開", "{0}: incumbent {2} trails {1}", "{0}：現任 {2} 落後 {1}"}},
      {"ev.close.title", {"大接戦", "Too close", "差距膠著"}},
      {"ev.close.text", {"{0}：{1}と{2}の差わずか{3}票", "{0}: {1} and {2} just {3} votes apart", "{0}：{1} 與 {2} 僅差 {3} 票"}},
      {"ev.final.title", {"開票終了", "Count complete", "開票完畢"}},
      {"ev.final.text", {"{0}：開票終了、{1}が{3}票差で当選", "{0}: count complete, {1} wins by {3}", "{0}：開票完畢，{1} 以 {3} 票差當選"}},
      {"ev.pr_seats.title", {"比例議席", "PR seats", "比例席次"}},
      {"ev.pr_seats.text", {"{0}：{1}が{3}議席", "{0}: {1} wins {3} seats", "{0}：{1} 獲得 {3} 席"}},
      {"ev.banner.called", {"当選確実 {0}人", "{0} projected winners", "當選確定 {0} 人"}},
      {"pin.pinned", {"ホームに固定", "Pinned as home", "已釘選為首頁"}},
      {"pin.unpinned", {"固定を解除", "Home unpinned", "已取消釘選"}},
      {"news.title", {"ニュース評判", "News sentiment", "新聞輿情"}},
      {"news.totals", {"好材料 {0} · 悪材料 {1} · 中立 {2}", "Good {0} · bad {1} · neutral {2}", "利多 {0} · 利空 {1} · 中性 {2}"}},
      {"news.disabled", {"ニュース無効：--news で起動（OpenAI互換API）", "News off: start with --news (OpenAI-compatible API)", "未啟用新聞：以 --news 啟動（OpenAI 相容 API）"}},
      {"news.good", {"ニュース · 好材料", "News · good", "新聞 · 利多"}},
      {"news.bad", {"ニュース · 悪材料", "News · bad", "新聞 · 利空"}},
      {"news.neutral", {"ニュース · 中立", "News · neutral", "新聞 · 中性"}},
      {"news.mock", {"模擬", "MOCK", "模擬"}},
      {"chart.map", {"地図", "Map", "地圖"}},
      {"chart.trend", {"得票推移", "Trend", "得票走勢"}},
      {"chart.seats", {"議席図", "Seats", "席次半圓"}},
      {"chart.margins", {"接戦順", "Margins", "差距排行"}},
      {"chart.parties", {"政党別", "Parties", "政黨得票"}},
      {"chart.grid", {"選挙区一覧", "Districts", "選區總覽"}},
      {"chart.back", {"M / Esc 地図に戻る", "M / Esc back to map", "M / Esc 回到地圖"}},
      {"chart.nodata", {"開票データなし", "No votes counted yet", "尚無開票資料"}},
      {"pip.title", {"最新ニュース", "Latest", "最新快訊"}},
      // Status / header.
      {"status.simulation", {"模擬データ SIMULATION", "SIMULATION · synthetic data", "模擬資料 SIMULATION"}},
      {"status.replay", {"再現 REPLAY · 実際の結果", "REPLAY · official results", "重播 REPLAY · 實際結果"}},
      {"status.voting", {"投票中 {0}–{1}", "Polls open {0}–{1}", "投票進行中 {0}–{1}"}},
      {"status.awaiting", {"開票データ待ち", "Awaiting results", "等待開票資料"}},
      {"status.countdown", {"投票日まで {0} 日", "{0} days to election day", "距投票日 {0} 天"}},
      {"status.countdown_hours", {"投票締切まで {0}", "Polls close in {0}", "距投票截止 {0}"}},
      {"status.preelection", {"投票前", "Pre-election", "投票前"}},
      {"status.counting", {"開票中", "Counting", "開票中"}},
      {"status.final", {"確定", "Final", "開票完成"}},
      {"header.subtitle",
       {"{0}（日）投票 {1}–{2} · 定数465（小選挙区289・比例176）",
        "Vote {0} (Sun) {1}–{2} · 465 seats (289 districts + 176 PR)",
        "{0}（日）投票 {1}–{2} · 總席次465（小選區289・比例176）"}},
      {"header.dissolution", {"解散 {0}", "Dissolved {0}", "解散 {0}"}},
      // Seats.
      {"seats.total", {"定数 {0}", "{0} seats", "總席次 {0}"}},
      {"seats.majority", {"過半数 {0}", "Majority {0}", "過半數 {0}"}},
      {"seats.supermajority", {"3分の2 {0}", "Two-thirds {0}", "三分之二 {0}"}},
      {"seats.smd", {"小選挙区", "District", "小選區"}},
      {"seats.pr", {"比例", "PR", "比例"}},
      {"seats.sum", {"計", "Total", "合計"}},
      {"seats.projected", {"当選確実", "Projected", "當選確定"}},
      {"seats.leading", {"リード中", "Leading", "領先中"}},
      {"seats.decided", {"確定", "Decided", "確定"}},
      {"seats.split", {"確実 {0} · リード {1}", "{0} won · {1} ahead", "確定 {0} · 領先 {1}"}},
      {"seats.previous", {"公示前 {0}", "Before {0}", "改選前 {0}"}},
      // Levels / area.
      {"level.nation", {"全国", "Nationwide", "全國"}},
      {"level.county", {"都道府県", "Prefecture", "都道府縣"}},
      {"level.town", {"小選挙区", "District", "小選區"}},
      {"level.village", {"市区町村", "Municipality", "市區町村"}},
      {"level.bloc", {"比例ブロック", "PR bloc", "比例區"}},
      {"area.title", {"地域の概要", "Area overview", "本區概況"}},
      {"area.size", {"面積 {0} km²", "Area {0} km²", "面積 {0} km²"}},
      {"area.districts", {"{0} 選挙区", "{0} districts", "{0} 個選區"}},
      {"area.units", {"{0} 市区町村", "{0} municipalities", "{0} 個市區町村"}},
      {"area.incumbent", {"前職", "Incumbent", "現任"}},
      // Status marks (新前元).
      {"status.new", {"新", "New", "新"}},
      {"status.incumbent", {"前", "Inc.", "現任"}},
      {"status.former", {"元", "Former", "前任"}},
      // National review.
      {"review.title", {"最高裁判所裁判官 国民審査", "Supreme Court national review", "最高法院法官國民審查"}},
      {"review.agree", {"罷免を可とする", "Dismiss", "應罷免"}},
      {"review.disagree", {"可としない", "Retain", "不罷免"}},
      {"review.dismiss_share", {"罷免可 {0}", "Dismiss {0}", "罷免 {0}"}},
      {"review.rule", {"罷免を可とする票が過半数で罷免", "Dismissed if a majority votes to dismiss", "罷免票過半數即罷免"}},
      {"review.retained", {"信任", "Retained", "續任"}},
      {"review.dismissed", {"罷免", "Dismissed", "罷免"}},
      {"review.prefecture_level", {"都道府県単位の集計", "Reported by prefecture", "以都道府縣為單位計票"}},
      // Tooltips / map.
      {"tip.no_votes_enter", {"開票データなし · クリックで詳細", "No votes yet · click to open", "尚無開票數據 · 點擊進入"}},
      {"tip.no_votes", {"開票データなし", "No votes yet", "尚無開票數據"}},
      {"tip.enter", {"クリックで詳細", "Click to open", "點擊進入"}},
      {"inset", {"位置は模式的", "Not to position", "示意位置"}},
      {"credits.map", {"地図：{0}", "Map: {0}", "地圖：{0}"}},
      {"credits.photos", {"写真：Wikimedia Commons ほか（assets/photos/CREDITS.json）", "Photos: Wikimedia Commons et al. (assets/photos/CREDITS.json)", "照片：Wikimedia Commons 等（assets/photos/CREDITS.json）"}},
      // Colour modes.
      {"mode.leader", {"小選挙区 リード政党", "District leader", "小選區領先政黨"}},
      {"mode.pr", {"比例 第1党", "PR leading party", "比例第一大黨"}},
      {"mode.margin", {"得票差", "Margin", "得票差距"}},
      {"mode.turnout", {"投票率", "Turnout", "投票率"}},
      {"mode.review", {"国民審査", "National review", "國民審查"}},
      {"footer.controls",
       {"左ドラッグ 移動 · 右ドラッグ 回転/傾き · ホイール ズーム · クリック 詳細 · Esc 戻る · 1–5 "
        "配色：{0} · F2–F6/G グラフ · I PiP · N ニュース · P 固定 · L 言語 · H ヘルプ",
        "Left-drag pan · right-drag orbit/tilt · wheel zoom · click drill down · Esc up · 1–5 "
        "colour: {0} · F2–F6/G charts · I PiP · N news · P pin · L language · H help",
        "左鍵拖曳 平移 · 右鍵拖曳 旋轉/傾斜 · 滾輪 縮放 · 點擊 進入下一層 · Esc 返回 · 1–5 著色：{0} · "
        "F2–F6/G 圖表 · I 子母畫面 · N 新聞 · P 釘選 · L 語言 · H 說明"}},
      {"help.title", {"操作方法", "Controls", "操作說明"}},
      {"help.1", {"左ドラッグ — 地図を移動", "Left-drag — pan the map", "左鍵拖曳 — 平移地圖"}},
      {"help.2", {"右 / 中ドラッグ — 回転・傾き", "Right/middle-drag — orbit & tilt", "右鍵 / 中鍵拖曳 — 旋轉與傾斜"}},
      {"help.3", {"ホイール、+ / - — ズーム", "Wheel, + / - — zoom to cursor", "滾輪、+ / - — 縮放"}},
      {"help.4",
       {"クリック — 都道府県 → 小選挙区 → 市区町村へ", "Click — drill into prefecture → district → municipality",
        "左鍵點擊 — 進入都道府縣 → 小選區 → 市區町村"}},
      {"help.5",
       {"Esc / Backspace / 右クリック — 一つ上へ", "Esc / Backspace / right-click — go up a level",
        "Esc / Backspace / 右鍵點擊 — 返回上一層"}},
      {"help.6",
       {"矢印 / WASD — 移動 · Q / E — 回転 · R / F — 傾き", "Arrows / WASD — pan · Q / E — rotate · R / F — tilt",
        "方向鍵 / WASD — 平移 · Q / E — 旋轉 · R / F — 傾斜"}},
      {"help.7",
       {"1 小選挙区 · 2 比例 · 3 得票差 · 4 投票率 · 5 国民審査（再押しで裁判官切替）",
        "1 district leader · 2 PR · 3 margin · 4 turnout · 5 national review (again: next justice)",
        "1 小選區 · 2 比例 · 3 差距 · 4 投票率 · 5 國民審查（再按切換法官）"}},
      {"help.8",
       {"Space 一時停止 · , / Page Down 減速 · . / Page Up 加速 · [ / ] 30分戻す/進める · P ホーム固定",
        "Space pause · , / Page Down slower · . / Page Up faster · [ / ] ∓30 min · P pin as home",
        "Space 暫停 · , / Page Down 減速 · . / Page Up 加速 · [ / ] 倒轉/快轉 30 分 · P 釘選首頁"}},
      {"help.9",
       {"L — 言語切替（日本語 / English / 繁中）· V — BGM · Home — ホームへ · N — ニュース · H — 閉じる",
        "L — language (日本語 / English / 繁中) · V — music · Home — home region · N — news · H — close",
        "L — 切換語言（日本語 / English / 繁中）· V — 背景音樂 · Home — 回首頁區域 · N — 新聞 · H — 說明"}},
      {"help.10", {"F7 — シミュレーションを再スタート", "F7 — restart simulation", "F7 — 重新開始模擬開票"}},
      {"simulation.restarted", {"シミュレーションを再スタート", "Simulation restarted", "模擬開票已重新開始"}},

      // --- Dashboard panels (i18n_dashboard.inc) -----------------------------
#include "src/ui/i18n_dashboard.inc"

      // --- Live layer and charts (i18n_live.inc) -----------------------------
#include "src/ui/i18n_live.inc"
  };
  return *table;
}

}  // namespace

bool ParseLang(std::string_view s, Lang* out) {
  std::string l;
  for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (l == "zh" || l == "zh-tw" || l == "zh_tw" || l == "tw" || l == "zh-hant") {
    *out = Lang::kZhTW;
  } else if (l == "ja" || l == "jp" || l == "ja-jp" || l == "ja_jp") {
    *out = Lang::kJa;
  } else if (l == "en" || l == "en-us" || l == "en_us" || l == "en-gb") {
    *out = Lang::kEn;
  } else {
    return false;
  }
  return true;
}

const char* LangCode(Lang l) {
  switch (l) {
    case Lang::kEn: return "en";
    case Lang::kZhTW: return "zh-TW";
    default: return "ja";
  }
}

const char* LangNativeName(Lang l) {
  switch (l) {
    case Lang::kEn: return "English";
    case Lang::kZhTW: return "繁體中文";
    default: return "日本語";
  }
}

Lang NextLang(Lang l) {
  return static_cast<Lang>((static_cast<int>(l) + 1) % static_cast<int>(Lang::kCount));
}

const char* Tr(Lang lang, std::string_view key) {
  const auto& t = Table();
  auto it = t.find(key);
  if (it == t.end()) return key.data();
  int i = static_cast<int>(lang);
  if (i < 0 || i >= static_cast<int>(Lang::kCount)) i = 0;  // unknown: Japanese
  const char* s = it->second[i];
  return s && *s ? s : it->second[0];
}

bool HasKey(std::string_view key) { return Table().count(key) > 0; }

std::vector<std::string_view> AllKeys() {
  std::vector<std::string_view> keys;
  for (const auto& [key, entry] : Table()) keys.push_back(key);
  return keys;
}

std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args) {
  std::string out;
  out.reserve(pattern.size() + 16);
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
        std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
      const size_t idx = pattern[i + 1] - '0';
      if (idx < args.size()) out += *(args.begin() + idx);
      i += 2;
    } else {
      out += pattern[i];
    }
  }
  return out;
}

namespace {

const std::string& Pick(Lang lang, const std::string& ja, const std::string& en,
                        const std::string& zh) {
  switch (lang) {
    case Lang::kEn: return en.empty() ? ja : en;
    case Lang::kZhTW: return zh.empty() ? ja : zh;
    default: return ja.empty() ? (en.empty() ? zh : en) : ja;
  }
}

}  // namespace

std::string Localizer::RegionName(const geo::Region& r) const {
  if (r.level == geo::Level::kNation) return T("nation.name");
  return Pick(lang_, r.name_ja, r.name_en, r.name_zh);
}

std::string Localizer::CandidateName(const election::Candidate& c) const {
  return Pick(lang_, c.name_ja, c.name_en, c.name_zh);
}

std::string Localizer::CandidateAltName(const election::Candidate& c) const {
  switch (lang_) {
    case Lang::kEn: return c.name_ja;
    default: return c.kana;
  }
}

std::string Localizer::ListEntryName(const election::ListEntry& e) const { return e.name_ja; }

std::string Localizer::PartyShort(const election::Party& p) const {
  switch (lang_) {
    case Lang::kEn: return p.short_en.empty() ? p.code : p.short_en;
    case Lang::kZhTW: return p.short_zh.empty() ? p.short_ja : p.short_zh;
    default: return p.short_ja.empty() ? p.name_ja : p.short_ja;
  }
}

std::string Localizer::PartyName(const election::Party& p) const {
  return Pick(lang_, p.name_ja, p.name_en, p.name_zh);
}

std::string Localizer::RaceTitle(const election::Race& r) const {
  return Pick(lang_, r.name_ja, r.name_en, r.name_zh);
}

std::string Localizer::BlocName(const election::Bloc& b) const {
  return Pick(lang_, b.name_ja, b.name_en, b.name_zh);
}

std::string Localizer::OfficeName(const election::OfficeSummary& o) const {
  return Pick(lang_, o.name_ja, o.name_en, o.name_zh);
}

std::string Localizer::ElectionName(const election::ElectionInfo& info) const {
  return Pick(lang_, info.name_ja, info.name_en, info.name_zh);
}

std::string Localizer::Justice(const election::Referendum& r) const {
  return Pick(lang_, r.justice_ja, r.justice_en, r.justice_zh);
}

std::string Localizer::Question(const election::Referendum& r) const {
  return Pick(lang_, r.question_ja, r.question_en, r.question_zh);
}

std::string Localizer::Bio(const election::Referendum& r) const {
  return Pick(lang_, r.bio_ja, r.bio_en, r.bio_zh);
}

std::string Localizer::Status(const std::string& status_ja) const {
  if (status_ja == "新") return T("status.new");
  if (status_ja == "前") return T("status.incumbent");
  if (status_ja == "元") return T("status.former");
  return status_ja;
}

std::string Localizer::ListSeparator() const { return lang_ == Lang::kEn ? ", " : "、"; }

}  // namespace jpy::ui
