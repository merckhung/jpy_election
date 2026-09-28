#include "src/news/mock_news.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace jpy::news {
namespace {

uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

struct Template {
  int phase;  // 0 = before 20:00 (final days of the campaign), 1 = counting night
  store::Sentiment sentiment;
  const char* title;
  const char* body;
  bool rival;  // mentions {2}
};

// {0} = candidate (ballot name), {1} = district ("東京1区"), {2} = a rival in
// the same district, {3} = the candidate's party (short name).
const Template kTemplates[] = {
    // Before the close of polls.
    {0, store::Sentiment::kGood, "{0}氏、終盤情勢で優勢　{1}",
     "{1}の終盤情勢調査で、{3}の{0}氏が優勢を保っている。陣営は最後まで支持拡大を呼びかけた。",
     false},
    {0, store::Sentiment::kGood, "{0}氏、最終日の街頭演説に大勢の聴衆　陣営に追い風",
     "{1}に立候補している{0}氏の選挙戦最終日の街頭演説には多くの聴衆が集まり、陣営は追い風を感じている。",
     false},
    {0, store::Sentiment::kGood, "{1}：{0}氏、無党派層にも支持拡大",
     "{1}では{0}氏が無党派層にも支持を広げている、と陣営はみている。", false},
    {0, store::Sentiment::kGood, "{1}：{0}氏が{2}氏をリード　終盤情勢",
     "終盤情勢調査によると、{1}では{0}氏が{2}氏を一歩リードしている。", true},
    {0, store::Sentiment::kBad, "{0}氏、終盤情勢で苦戦　{1}",
     "{1}の終盤情勢調査で、{0}氏は{2}氏を追う展開となっている。陣営は巻き返しを図る。", true},
    {0, store::Sentiment::kBad, "{0}氏、支持の広がりに伸び悩み",
     "{1}の{0}氏は支持の広がりに伸び悩み、陣営は厳しい戦いを認めた。", false},
    {0, store::Sentiment::kBad, "{0}氏の政策に{2}氏陣営が批判",
     "{1}で、{2}氏の陣営が{0}氏の政策を批判した。{0}氏側は説明を尽くすとしている。", true},
    {0, store::Sentiment::kBad, "{0}氏に逆風　{1}の終盤情勢",
     "{1}では{0}氏にとって厳しい選挙となっている。", false},
    {0, store::Sentiment::kNeutral, "{0}氏、地元で有権者と対話",
     "{1}に立候補している{0}氏は地元の商店街を回り、有権者と言葉を交わした。", false},
    {0, store::Sentiment::kNeutral, "{1}：{0}氏と{2}氏が公開討論会に出席",
     "{1}の{0}氏と{2}氏が公開討論会に出席し、それぞれの政策を訴えた。", true},
    {0, store::Sentiment::kNeutral, "{0}氏、子育て支援策を発表",
     "{3}の{0}氏は子育て支援を柱とする政策を発表した。", false},
    {0, store::Sentiment::kNeutral, "{1}：期日前投票進む　{0}氏の陣営も呼びかけ",
     "{1}では期日前投票が進み、{0}氏の陣営も投票を呼びかけた。", false},
    // Counting night (after 20:00).
    {1, store::Sentiment::kGood, "{0}氏、出口調査で優勢　{1}",
     "報道各社の出口調査によると、{1}では{0}氏が優勢となっている。", false},
    {1, store::Sentiment::kGood, "{1}：{0}氏が開票で先行、事務所に歓声",
     "{1}の開票で{0}氏が先行し、事務所には支持者の歓声が上がった。", false},
    {1, store::Sentiment::kGood, "{1}：{0}氏、{2}氏をリード",
     "開票が進む{1}で、{0}氏が{2}氏をリードしている。", true},
    {1, store::Sentiment::kGood, "{0}氏の陣営に手応え　{1}",
     "{1}の{0}氏の事務所では、開票が進むにつれて陣営幹部が手応えを口にした。", false},
    {1, store::Sentiment::kBad, "{0}氏、開票で苦戦　{1}",
     "{1}の開票で{0}氏は{2}氏を追う展開となり、苦戦している。", true},
    {1, store::Sentiment::kBad, "{0}氏陣営に厳しい表情　出口調査で劣勢",
     "出口調査で{0}氏は劣勢と伝えられ、{1}の事務所には重い空気が漂った。", false},
    {1, store::Sentiment::kBad, "{1}：{0}氏、票が伸び悩み",
     "{1}の{0}氏は開票が進んでも票が伸び悩み、陣営には厳しい戦いとなっている。", false},
    {1, store::Sentiment::kNeutral, "{1}：{0}氏の事務所、開票を見守る",
     "{1}の{0}氏の事務所では、支持者らが開票の行方を見守っている。", false},
    {1, store::Sentiment::kNeutral, "{1}で開票始まる　{0}氏、{2}氏らが立候補",
     "{1}で開票作業が始まった。{0}氏、{2}氏らが議席を争っている。", true},
    {1, store::Sentiment::kNeutral, "{0}氏、支持者とともに開票速報を見守る",
     "{1}の{0}氏は事務所で支持者とともにテレビの開票速報を見守った。", false},
};
constexpr size_t kTemplateCount = sizeof(kTemplates) / sizeof(kTemplates[0]);

std::string Fill(const char* pattern, const std::string& a, const std::string& b,
                 const std::string& c, const std::string& d) {
  std::string out;
  for (const char* p = pattern; *p; ++p) {
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '3' && p[2] == '}') {
      out += p[1] == '0' ? a : p[1] == '1' ? b : p[1] == '2' ? c : d;
      p += 2;
    } else {
      out += *p;
    }
  }
  return out;
}

// "2026-02-08T21:30:00+09:00" for `minute` after 20:00 on election day.
std::string PublishedAt(const std::string& date, double minute) {
  int y = 2026, m = 2, d = 8;
  std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d);
  const int total = static_cast<int>(std::floor(20 * 60 + minute));
  const int day = total >= 0 ? total / 1440 : -((-total + 1439) / 1440);
  const int in_day = total - day * 1440;
  using namespace std::chrono;
  const year_month_day ymd{sys_days{year{y} / month{static_cast<unsigned>(m)} /
                                    std::chrono::day{static_cast<unsigned>(d)}} +
                           days{day}};
  char when[40];
  std::snprintf(when, sizeof when, "%04d-%02u-%02uT%02d:%02d:00+09:00", static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()), in_day / 60,
                in_day % 60);
  return when;
}

}  // namespace

MockNewsGenerator::MockNewsGenerator(const election::ElectionData* data, uint64_t seed)
    : data_(data), seed_(seed) {
  // District candidates only; the main ones get more coverage: weight by
  // party size and incumbency.
  const auto& races = data_->races();
  for (size_t r = 0; r < races.size(); ++r) {
    if (!races[r].is_smd()) continue;
    for (size_t c = 0; c < races[r].candidates.size(); ++c) {
      const auto& cand = races[r].candidates[c];
      int weight = 1;
      const std::string& p = cand.party;
      if (p == "LDP" || p == "CRA" || p == "JIP" || p == "DPFP") weight += 3;
      else if (p == "SANSEI" || p == "JCP" || p == "REIWA" || p == "MIRAI" || p == "CPJ") weight += 1;
      if (cand.status == "前" || cand.incumbent) weight += 2;
      else if (cand.status == "元") weight += 1;
      for (int w = 0; w < weight; ++w) pool_.push_back({static_cast<int>(r), static_cast<int>(c)});
    }
  }
}

MockArticle MockNewsGenerator::Make(uint64_t h, double minute, int phase) {
  MockArticle m;
  if (pool_.empty()) return m;
  const auto& races = data_->races();
  const auto [ri, ci] = pool_[Mix(h + 1) % pool_.size()];
  const election::Race& race = races[ri];
  const election::Candidate& cand = race.candidates[ci];
  const bool has_rival = race.candidates.size() >= 2;
  int rival = has_rival ? static_cast<int>(Mix(h + 2) % race.candidates.size()) : ci;
  if (has_rival && rival == ci) rival = (rival + 1) % static_cast<int>(race.candidates.size());

  // Templates usable for this phase and race.
  size_t usable[kTemplateCount];
  size_t n = 0;
  for (size_t t = 0; t < kTemplateCount; ++t) {
    if (phase >= 0 && kTemplates[t].phase != phase) continue;
    if (kTemplates[t].rival && !has_rival) continue;
    usable[n++] = t;
  }
  const Template& t = kTemplates[usable[Mix(h + 3) % n]];
  const std::string& rival_name = race.candidates[rival].name_ja;
  const std::string party = data_->party(cand.party).short_ja;

  m.candidate_id = cand.id;
  m.intended = t.sentiment;
  m.article.title = "【模擬】" + Fill(t.title, cand.name_ja, race.name_ja, rival_name, party);
  m.article.summary = Fill(t.body, cand.name_ja, race.name_ja, rival_name, party) +
                      "（本記事はシステム試験用の模擬ニュースです。実際の報道ではありません。）";
  m.article.source = "MOCK";
  char url[96];
  std::snprintf(url, sizeof url, "mock://news/%016llx", static_cast<unsigned long long>(h));
  m.article.url = url;
  // Batch items (phase -1) get a time matching their template.
  if (phase < 0) minute = t.phase == 0 ? -120 : 90;
  m.article.published_at = PublishedAt(data_->info().date, minute);
  m.article.simulated = true;
  return m;
}

std::vector<MockArticle> MockNewsGenerator::Between(double from, double to, double per_hour) {
  std::vector<MockArticle> out;
  if (pool_.empty()) return out;
  // One potential item slot per simulated minute; deterministic per minute.
  for (int minute = static_cast<int>(std::ceil(from)); minute < to; ++minute) {
    const uint64_t h = Mix(seed_ ^ (static_cast<uint64_t>(minute + 100000) * 0x51ED27));
    if (Unit(h) < per_hour / 60.0) out.push_back(Make(h, minute, minute < 0 ? 0 : 1));
  }
  return out;
}

std::vector<MockArticle> MockNewsGenerator::Batch(int n) {
  std::vector<MockArticle> out;
  if (pool_.empty()) return out;
  for (int i = 0; i < n; ++i) out.push_back(Make(Mix(seed_ + 7919ull * (i + 1)), 0, -1));
  return out;
}

}  // namespace jpy::news
