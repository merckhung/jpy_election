#include "src/news/classifier.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <unordered_map>

#include "nlohmann/json.hpp"
#include "src/news/http.h"

namespace jpy::news {

using nlohmann::json;

namespace {

// ---------------------------------------------------------------------------
// UTF-8 helpers.

size_t CharLen(unsigned char c) {
  return c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
}

// Code point at `i` (0 at the end of the string).
char32_t CodePointAt(const std::string& s, size_t i, size_t* len = nullptr) {
  if (i >= s.size()) {
    if (len) *len = 0;
    return 0;
  }
  const unsigned char c = s[i];
  const size_t n = std::min(CharLen(c), s.size() - i);
  if (len) *len = n;
  if (n == 1) return c;
  char32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
  for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
  return cp;
}

// Code point ending just before byte `i` (0 at the start).
char32_t CodePointBefore(const std::string& s, size_t i) {
  if (i == 0) return 0;
  size_t b = i - 1;
  while (b > 0 && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) --b;
  return CodePointAt(s, b);
}

void AppendUtf8(std::string* out, char32_t cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool IsKanji(char32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || cp == 0x3005;  // 々
}
bool IsKatakana(char32_t cp) { return cp >= 0x30A1 && cp <= 0x30FA; }
bool IsHiragana(char32_t cp) { return cp >= 0x3041 && cp <= 0x309F; }

bool HasKanji(const std::string& s) {
  for (size_t i = 0, n = 0; i < s.size(); i += n) {
    if (IsKanji(CodePointAt(s, i, &n))) return true;
    if (n == 0) break;
  }
  return false;
}

size_t CodePoints(const std::string& s) {
  size_t count = 0;
  for (size_t i = 0; i < s.size(); i += CharLen(static_cast<unsigned char>(s[i]))) ++count;
  return count;
}

bool IsAsciiWordChar(char32_t cp) { return cp < 0x80 && (std::isalnum(static_cast<int>(cp)) != 0); }

bool IsAsciiWord(const std::string& w) {
  return std::all_of(w.begin(), w.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string RemoveSpaces(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c != ' ') out += c;
  }
  return out;
}

bool StartsWithAt(const std::string& s, size_t pos, const std::string& prefix) {
  return pos <= s.size() && s.compare(pos, prefix.size(), prefix) == 0;
}

// Finds `word` in `text` from `from`: ASCII words match case-insensitively
// (`lower` is the lower-cased text) on word boundaries; others literally.
size_t FindWord(const std::string& text, const std::string& lower, const std::string& word,
                size_t from = 0) {
  if (word.empty()) return std::string::npos;
  if (!IsAsciiWord(word)) return text.find(word, from);
  const std::string w = Lower(word);
  for (size_t p = lower.find(w, from); p != std::string::npos; p = lower.find(w, p + 1)) {
    if (IsAsciiWordChar(CodePointBefore(lower, p))) continue;
    if (IsAsciiWordChar(CodePointAt(lower, p + w.size()))) continue;
    return p;
  }
  return std::string::npos;
}

// Case-sensitive ASCII match on word boundaries (party abbreviations: "LDP").
size_t FindExactWord(const std::string& text, const std::string& word, size_t from = 0) {
  for (size_t p = text.find(word, from); p != std::string::npos; p = text.find(word, p + 1)) {
    if (IsAsciiWordChar(CodePointBefore(text, p))) continue;
    if (IsAsciiWordChar(CodePointAt(text, p + word.size()))) continue;
    return p;
  }
  return std::string::npos;
}

std::string ArticleText(const store::Article& a) {
  return NormalizeText(a.title + "\n" + a.summary);
}

// Sentence splitter for mixed Japanese/English text.
std::vector<std::string> Sentences(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < text.size();) {
    const size_t n = std::min(CharLen(static_cast<unsigned char>(text[i])), text.size() - i);
    const std::string ch = text.substr(i, n);
    cur += ch;
    i += n;
    if (ch == "。" || ch == "！" || ch == "？" || ch == "\n" || ch == "!" || ch == "?" ||
        (ch == "." && (i >= text.size() || text[i] == ' '))) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// ---------------------------------------------------------------------------
// Lexicon.

struct Word {
  const char* text;
  int weight;  // > 0 good, < 0 bad
};

// Plain sentiment words. Phrases that negate a good word ("当選ならず")
// weigh -2 so that together with the word they contain they net -1.
const std::vector<Word>& SentimentWords() {
  static const auto* w = new std::vector<Word>{
      // Good for the subject.
      {"優勢", 1}, {"当選確実", 1}, {"当確", 1}, {"当選", 1}, {"初当選", 1}, {"再選", 1},
      {"返り咲", 1}, {"比例復活", 1}, {"復活当選", 1}, {"勝利", 1}, {"圧勝", 1}, {"大勝", 1},
      {"完勝", 1}, {"快勝", 1}, {"躍進", 1}, {"好調", 1}, {"堅調", 1}, {"追い風", 1},
      {"支持拡大", 1}, {"支持を広げ", 1}, {"支持を集め", 1}, {"議席増", 1}, {"議席を伸ば", 1},
      {"伸長", 1}, {"健闘", 1}, {"善戦", 1}, {"リード", 1}, {"先行", 1}, {"優位", 1},
      {"逆転", 1}, {"抜け出", 1}, {"手堅", 1}, {"手応え", 1}, {"万歳", 1},
      {"過半数を確保", 1}, {"単独過半数", 1}, {"評価", 1}, {"称賛", 1}, {"歓声", 1},
      {"応援", 1}, {"推薦", 1}, {"追い上げ", 1}, {"猛追", 1},
      {"lead", 1}, {"leads", 1}, {"leading", 1}, {"wins", 1}, {"won", 1}, {"victory", 1},
      {"landslide", 1}, {"surge", 1}, {"gains", 1}, {"ahead", 1}, {"endorsed", 1},
      {"endorsement", 1}, {"praised", 1},
      // Bad for the subject.
      {"劣勢", -1}, {"苦戦", -1}, {"落選", -1}, {"失言", -1}, {"逆風", -1}, {"不祥事", -1},
      {"批判", -1}, {"敗北", -1}, {"惨敗", -1}, {"大敗", -1}, {"完敗", -1}, {"敗戦", -1},
      {"敗れ", -1}, {"及ばず", -1}, {"届かず", -1}, {"伸び悩", -1}, {"低迷", -1}, {"失速", -1},
      {"出遅れ", -1}, {"謝罪", -1}, {"陳謝", -1}, {"疑惑", -1}, {"裏金", -1}, {"辞任", -1},
      {"離党", -1}, {"炎上", -1}, {"物議", -1}, {"問題視", -1}, {"追及", -1}, {"反発", -1},
      {"不信", -1}, {"厳しい戦い", -1}, {"厳しい選挙", -1}, {"苦しい戦い", -1},
      {"議席減", -1}, {"議席を減ら", -1}, {"議席を失", -1}, {"過半数割れ", -1},
      {"過半数を割", -1}, {"不調", -1}, {"引責", -1}, {"失望", -1},
      // Negated good words.
      {"当選ならず", -2}, {"勝利ならず", -2}, {"比例復活ならず", -2}, {"当選を逃", -2},
      {"逆転負け", -2}, {"逆転を許", -2}, {"逆転され", -2}, {"リードを許", -2},
      {"リードされ", -2}, {"先行を許", -2}, {"先行され", -2},
      {"trails", -1}, {"trailing", -1}, {"behind", -1}, {"scandal", -1}, {"loses", -1},
      {"lost", -1}, {"defeat", -1}, {"defeated", -1}, {"concedes", -1}, {"conceded", -1},
      {"gaffe", -1}, {"resigns", -1}, {"resigned", -1}, {"criticized", -1},
      {"criticised", -1}, {"setback", -1}, {"slush", -1}};
  return *w;
}

// Relations between two names in one sentence. Japanese is verb-final
// ("AがBを逆転した"), so these are searched after the first name and the
// roles come from the particles; English ones ("A leads B") must sit between
// the names, subject first.
const std::vector<std::string>& AheadWords() {
  static const auto* w = new std::vector<std::string>{
      "逆転", "リード", "上回", "引き離", "競り勝", "破っ", "破り", "破る", "下し", "下す",
      "制し", "制す", "勝利", "先行", "優位", "優勢", "抑え", "leads", "overtakes", "beats",
      "defeats", "ahead of"};
  return *w;
}

const std::vector<std::string>& BehindWords() {
  static const auto* w = new std::vector<std::string>{
      "敗れ", "及ばず", "届かず", "を追う", "を追い", "下回", "競り負", "後れ", "遅れ",
      "劣勢", "破られ", "trails", "behind"};
  return *w;
}

// Titles and honorifics after a name ("高市首相", "野田代表", "玉木氏").
const std::vector<std::string>& Titles() {
  static const auto* w = [] {
    auto* v = new std::vector<std::string>{
        "氏", "さん", "候補", "首相", "前首相", "元首相", "総理", "元総理", "代表", "共同代表",
        "代表代行", "総裁", "副総裁", "幹事長", "政調会長", "総務会長", "選対委員長", "委員長",
        "党首", "官房長官", "大臣", "外相", "財務相", "防衛相", "経産相", "総務相", "農相",
        "農水相", "厚労相", "文科相", "国交相", "法相", "環境相", "デジタル相", "議員", "前議員",
        "元議員", "衆院議員", "前衆院議員", "元衆院議員", "前職", "元職", "新人", "陣営",
        "事務所"};
    // Longest first, so "前首相" wins over "前職"-style prefixes.
    std::sort(v->begin(), v->end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return v;
  }();
  return *w;
}

size_t TitleAt(const std::string& s, size_t pos) {
  for (const std::string& t : Titles()) {
    if (StartsWithAt(s, pos, t)) return t.size();
  }
  return 0;
}

int SentimentScore(const std::string& s) {
  const std::string lower = Lower(s);
  int score = 0;
  for (const Word& w : SentimentWords()) {
    if (w.weight == 0) continue;
    const std::string word = w.text;
    for (size_t p = FindWord(s, lower, word); p != std::string::npos;
         p = FindWord(s, lower, word, p + word.size())) {
      score += w.weight;
    }
  }
  return score;
}

// ---------------------------------------------------------------------------
// Parties.

struct PartyEntry {
  const char* code;
  const char* name;                 // full Japanese name
  std::vector<std::string> keywords;  // names, abbreviations, leaders
};

// Keywords per party (codes of data/election/2026/parties.json). Ambiguous
// abbreviations are left out on purpose: 国民 (国民審査, 国民の...),
// 中道 alone, 保守 alone, 減税 (a policy), みらい alone.
const std::vector<PartyEntry>& PartyTable() {
  static const auto* t = new std::vector<PartyEntry>{
      {"LDP", "自由民主党",
       {"自由民主党", "自民党", "自民", "高市早苗", "高市首相", "高市総裁", "高市総理", "LDP",
        "Liberal Democratic Party", "Takaichi"}},
      // Formed in January 2026 by the Constitutional Democrats and Komeito.
      {"CRA", "中道改革連合",
       {"中道改革連合", "中道改革", "中道新党", "立憲民主党", "立憲民主", "公明党", "野田佳彦",
        "野田代表", "斉藤鉄夫", "斉藤代表", "Centrist Reform Alliance"}},
      {"JIP", "日本維新の会",
       {"日本維新の会", "維新の会", "維新", "吉村洋文", "吉村代表", "藤田文武", "藤田共同代表",
        "Ishin", "Japan Innovation Party"}},
      {"DPFP", "国民民主党",
       {"国民民主党", "国民民主", "玉木雄一郎", "玉木代表", "DPFP",
        "Democratic Party for the People"}},
      {"SANSEI", "参政党", {"参政党", "参政", "神谷宗幣", "神谷代表", "Sanseito"}},
      {"JCP", "日本共産党",
       {"日本共産党", "共産党", "共産", "田村智子", "田村委員長", "JCP", "Communist Party"}},
      {"REIWA", "れいわ新選組",
       {"れいわ新選組", "れいわ", "山本太郎", "Reiwa Shinsengumi", "Reiwa"}},
      {"GENZEI", "減税日本・ゆうこく連合",
       {"減税日本", "ゆうこく連合", "河村たかし", "原口一博"}},
      {"CPJ", "日本保守党",
       {"日本保守党", "保守党", "百田尚樹", "百田代表", "Conservative Party of Japan"}},
      {"SDP", "社会民主党", {"社会民主党", "社民党", "社民", "福島瑞穂", "Social Democratic Party"}},
      {"MIRAI", "チームみらい", {"チームみらい", "安野貴博", "安野党首", "Team Mirai"}},
      {"EUTH", "安楽死制度を考える会", {"安楽死制度を考える会"}},
      {"SAISEI", "再生の道", {"再生の道", "石丸伸二"}},
      {"MUREN", "無所属連合", {"無所属連合"}},
      {"YAMATO", "日本大和党", {"日本大和党"}},
      {"WPP", "世界平和党", {"世界平和党"}},
      {"FUSION", "核融合党", {"核融合党"}},
      {"MIRASHIN", "未来進歩党", {"未来進歩党"}},
      {"JFP", "日本自由党", {"日本自由党"}},
      {"KOKORO", "心の党", {"心の党"}},
  };
  return *t;
}

const PartyEntry* PartyByCode(const std::string& code) {
  for (const PartyEntry& p : PartyTable()) {
    if (code == p.code) return &p;
  }
  return nullptr;
}

// Words that contain a party keyword without meaning the party.
const std::vector<std::string>& PartyExclusions() {
  static const auto* w = new std::vector<std::string>{
      "参政権", "明治維新", "維新後", "共産主義", "共産圏", "中国共産党", "保守党政権",
      "英保守党", "英国保守党", "社民主義"};
  return *w;
}

bool Excluded(const std::string& text, size_t pos, const std::string& kw) {
  for (const std::string& e : PartyExclusions()) {
    for (size_t k = e.find(kw); k != std::string::npos; k = e.find(kw, k + 1)) {
      if (k <= pos && StartsWithAt(text, pos - k, e)) return true;
    }
  }
  return false;
}

size_t FindPartyKeyword(const std::string& text, const std::string& kw, size_t from = 0) {
  if (IsAsciiWord(kw)) return FindExactWord(text, kw, from);
  for (size_t p = text.find(kw, from); p != std::string::npos; p = text.find(kw, p + 1)) {
    if (!Excluded(text, p, kw)) return p;
  }
  return std::string::npos;
}

// ---------------------------------------------------------------------------
// Matching names inside a sentence.

struct Match {
  size_t pos = std::string::npos;
  size_t len = 0;  // the name itself (titles after it are not included)
  bool found() const { return pos != std::string::npos; }
};

void Earliest(Match* best, size_t pos, size_t len) {
  if (pos == std::string::npos) return;
  if (!best->found() || pos < best->pos || (pos == best->pos && len > best->len)) {
    best->pos = pos;
    best->len = len;
  }
}

// "高市首相": surname followed by a title, not preceded by a kanji or
// katakana (so "林氏" does not match inside "小林氏").
size_t FindSurnameWithTitle(const std::string& s, const std::string& surname, size_t from = 0) {
  if (surname.empty()) return std::string::npos;
  for (size_t p = s.find(surname, from); p != std::string::npos; p = s.find(surname, p + 1)) {
    const char32_t before = CodePointBefore(s, p);
    if (IsKanji(before) || IsKatakana(before)) continue;
    if (TitleAt(s, p + surname.size()) > 0) return p;
  }
  return std::string::npos;
}

Match FindName(const std::string& s, const std::string& lower, const CandidateRef& c,
               bool allow_surname) {
  Match m;
  for (const std::string& n : c.names) Earliest(&m, s.find(n), n.size());
  if (c.name_en.size() >= 5) {
    const std::string en = Lower(c.name_en);
    Earliest(&m, FindWord(s, lower, en), en.size());
  }
  if (allow_surname && !c.surname.empty()) {
    Earliest(&m, FindSurnameWithTitle(s, c.surname), c.surname.size());
  }
  return m;
}

Match FindParty(const std::string& s, const std::string& code) {
  Match m;
  const PartyEntry* p = PartyByCode(code);
  if (!p) return m;
  for (const std::string& kw : p->keywords) Earliest(&m, FindPartyKeyword(s, kw), kw.size());
  return m;
}

// Grammatical role of a name from the particle after it (and its title).
enum class Role { kUnknown, kSubject, kObject };

Role RoleAfter(const std::string& s, const Match& m) {
  size_t p = m.pos + m.len;
  for (int k = 0; k < 3; ++k) {
    const size_t t = TitleAt(s, p);
    if (t == 0) break;
    p += t;
  }
  while (p < s.size() && s[p] == ' ') ++p;
  size_t n = 0;
  const char32_t cp = CodePointAt(s, p, &n);
  if (cp == U'が' || cp == U'は' || cp == U'も' || cp == U'、' || cp == U',' || cp == U'：') {
    // "AとBが..." style subjects are handled by the caller's fallback.
    return Role::kSubject;
  }
  if (cp == U'を' || cp == U'に' || cp == U'へ') return Role::kObject;
  return Role::kUnknown;
}

// +2 when `a` is ahead of `b` in this sentence, -2 when behind, 0 when the
// sentence does not compare them.
int Comparative(const std::string& s, const std::string& lower, const Match& a, const Match& b) {
  const size_t first = std::min(a.pos, b.pos), second = std::max(a.pos, b.pos);
  size_t best = std::string::npos, best_len = 0;
  int dir = 0;
  bool japanese = false;
  auto consider = [&](const std::string& w, int d) {
    size_t p;
    if (IsAsciiWord(w)) {
      p = FindWord(s, lower, w, first);
      if (p == std::string::npos || p >= second) return;
    } else {
      p = s.find(w, first);
      // Skip occurrences inside the names themselves.
      while (p != std::string::npos && ((p >= a.pos && p < a.pos + a.len) ||
                                        (p >= b.pos && p < b.pos + b.len))) {
        p = s.find(w, p + 1);
      }
      if (p == std::string::npos) return;
    }
    if (best == std::string::npos || p < best) {
      best = p;
      best_len = w.size();
      dir = d;
      japanese = !IsAsciiWord(w);
    }
  };
  for (const std::string& w : AheadWords()) consider(w, +1);
  for (const std::string& w : BehindWords()) consider(w, -1);
  if (dir == 0) return 0;
  const bool a_first = a.pos < b.pos;
  bool a_subject = a_first;
  if (japanese) {
    // Passive / concession after the verb inverts it: "逆転された", "リードを許す".
    const size_t after = best + best_len;
    if (StartsWithAt(s, after, "され") || StartsWithAt(s, after, "られ") ||
        StartsWithAt(s, after, "を許")) {
      dir = -dir;
    }
    const Role ra = RoleAfter(s, a), rb = RoleAfter(s, b);
    if (ra == Role::kSubject && rb != Role::kSubject) a_subject = true;
    else if (ra == Role::kObject && rb != Role::kObject) a_subject = false;
    else if (rb == Role::kSubject) a_subject = false;
    else if (rb == Role::kObject) a_subject = true;
  }
  return (a_subject ? dir : -dir) * 2;
}

// Keyword score of each entity over the sentences: comparative phrases with
// another entity count +-2, otherwise the plain sentiment words count.
std::vector<int> ScoreEntities(const std::vector<std::string>& sentences,
                               const std::vector<std::function<Match(const std::string&,
                                                                     const std::string&)>>& find) {
  std::vector<int> scores(find.size(), 0);
  for (const std::string& s : sentences) {
    const std::string lower = Lower(s);
    std::vector<Match> m(find.size());
    for (size_t i = 0; i < find.size(); ++i) m[i] = find[i](s, lower);
    int plain = 0;
    bool plain_done = false;
    for (size_t i = 0; i < find.size(); ++i) {
      if (!m[i].found()) continue;
      bool comparative = false;
      for (size_t j = 0; j < find.size(); ++j) {
        if (j == i || !m[j].found()) continue;
        // Same span (e.g. a party keyword that is also a leader's name).
        if (m[j].pos == m[i].pos) continue;
        const int c = Comparative(s, lower, m[i], m[j]);
        if (c != 0) {
          scores[i] += c;
          comparative = true;
        }
      }
      if (!comparative) {
        if (!plain_done) {
          plain = SentimentScore(s);
          plain_done = true;
        }
        scores[i] += plain;
      }
    }
  }
  return scores;
}

store::Sentiment FromScore(int score) {
  return score > 0 ? store::Sentiment::kGood
         : score < 0 ? store::Sentiment::kBad
                     : store::Sentiment::kNeutral;
}

std::string ScoreReason(int score) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "キーワード判定 %+d", score);
  return buf;
}

bool ContainsRaceTitle(const std::string& text, const std::string& title) {
  if (title.empty()) return false;
  for (size_t p = text.find(title); p != std::string::npos; p = text.find(title, p + 1)) {
    const char32_t next = CodePointAt(text, p + title.size());
    if (!(next >= U'0' && next <= U'9')) return true;  // "北海道1区" is not "北海道10区"
  }
  return false;
}

}  // namespace

std::string NormalizeText(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    size_t n = 0;
    char32_t cp = CodePointAt(s, i, &n);
    if (n == 0) break;
    if (n == 1) {
      out.push_back(s[i]);
      i += 1;
      continue;
    }
    if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;  // full-width ASCII
    else if (cp == 0x3000) cp = ' ';                 // ideographic space
    else if (cp == U'髙') cp = U'高';
    else if (cp == U'﨑') cp = U'崎';
    else if (cp == U'德') cp = U'徳';
    AppendUtf8(&out, cp);
    i += n;
  }
  return out;
}

const std::vector<std::string>& PartyKeywords(const std::string& code) {
  static const std::vector<std::string> kEmpty;
  const PartyEntry* p = PartyByCode(code);
  return p ? p->keywords : kEmpty;
}

std::vector<std::string> MentionedParties(const store::Article& a) {
  const std::string text = ArticleText(a);
  std::vector<std::pair<std::string, std::pair<int, size_t>>> found;  // code, (count, first)
  for (const PartyEntry& p : PartyTable()) {
    int count = 0;
    size_t first = std::string::npos;
    for (const std::string& kw : p.keywords) {
      for (size_t pos = FindPartyKeyword(text, kw); pos != std::string::npos;
           pos = FindPartyKeyword(text, kw, pos + kw.size())) {
        ++count;
        first = std::min(first, pos);
      }
    }
    if (count) found.push_back({p.code, {count, first}});
  }
  std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) {
    if (x.second.first != y.second.first) return x.second.first > y.second.first;
    return x.second.second < y.second.second;
  });
  std::vector<std::string> out;
  for (const auto& f : found) out.push_back(f.first);
  return out;
}

std::vector<CandidateRef> CandidateRefs(const election::ElectionData& data) {
  std::vector<CandidateRef> out;
  auto add_name = [](CandidateRef* r, const std::string& raw) {
    const std::string n = NormalizeText(raw);
    if (CodePoints(n) < 2) return;
    if (std::find(r->names.begin(), r->names.end(), n) == r->names.end()) r->names.push_back(n);
  };
  for (const election::Race& race : data.races()) {
    if (!race.is_smd()) continue;
    for (const election::Candidate& c : race.candidates) {
      CandidateRef r;
      r.id = c.id;
      r.name = c.name_ja;
      add_name(&r, c.name_ja);
      add_name(&r, c.name_legal);
      add_name(&r, c.surname_ja + c.given_ja);
      const std::string kana = NormalizeText(c.kana);
      if (CodePoints(RemoveSpaces(kana)) >= 4) {
        add_name(&r, kana);
        add_name(&r, RemoveSpaces(kana));
      }
      r.name_en = c.name_en;
      // Kanji surname: the ballot surname unless written in kana
      // ("たまき雄一郎"), then the registered name minus the given name.
      std::string surname = NormalizeText(c.surname_ja);
      if (!HasKanji(surname)) {
        const std::string legal = NormalizeText(c.name_legal), given = NormalizeText(c.given_ja);
        surname.clear();
        if (!given.empty() && legal.size() > given.size() &&
            legal.compare(legal.size() - given.size(), given.size(), given) == 0) {
          surname = legal.substr(0, legal.size() - given.size());
        }
      }
      r.surname = HasKanji(surname) ? surname : "";
      const election::Party& p = data.party(c.party);
      r.party = p.short_ja;
      r.party_code = c.party;
      r.race_title = race.name_ja;
      out.push_back(std::move(r));
    }
  }
  // Surname uniqueness among district candidates.
  std::unordered_map<std::string, int> by_surname, by_party_surname;
  for (const CandidateRef& r : out) {
    if (r.surname.empty()) continue;
    ++by_surname[r.surname];
    ++by_party_surname[r.party_code + "|" + r.surname];
  }
  for (CandidateRef& r : out) {
    if (r.surname.empty()) continue;
    r.unique_surname = by_surname[r.surname] == 1;
    r.unique_in_party = by_party_surname[r.party_code + "|" + r.surname] == 1;
  }
  // List-only PR names: one ref per party list.
  for (const election::Race& race : data.races()) {
    if (!race.is_pr()) continue;
    for (const election::Candidate& list : race.candidates) {
      CandidateRef r;
      r.id = list.id;
      r.list = true;
      const election::Party& p = data.party(list.party);
      r.name = p.short_ja + " " + race.name_ja + "名簿";
      r.party = p.short_ja;
      r.party_code = list.party;
      r.race_title = race.name_ja;
      for (const election::ListEntry& e : list.list) {
        if (!e.candidate.empty() || !e.smd.empty()) continue;
        add_name(&r, e.name_ja);
        add_name(&r, e.surname_ja + e.given_ja);
      }
      if (!r.names.empty()) out.push_back(std::move(r));
    }
  }
  return out;
}

std::vector<CandidateRef> MentionedCandidates(const store::Article& a,
                                              const std::vector<CandidateRef>& all) {
  const std::string text = ArticleText(a);
  const std::string lower = Lower(text);
  struct Occ {
    size_t ref, pos, len;
  };
  std::vector<Occ> occ;
  for (size_t i = 0; i < all.size(); ++i) {
    const CandidateRef& c = all[i];
    for (const std::string& n : c.names) {
      for (size_t p = text.find(n); p != std::string::npos; p = text.find(n, p + 1)) {
        occ.push_back({i, p, n.size()});
      }
    }
    if (c.name_en.size() >= 5) {
      const std::string en = Lower(c.name_en);
      for (size_t p = FindWord(text, lower, en); p != std::string::npos;
           p = FindWord(text, lower, en, p + 1)) {
        occ.push_back({i, p, en.size()});
      }
    }
  }
  // "Surname + title" mentions, when they identify one candidate.
  std::vector<std::string> parties;
  bool parties_done = false;
  std::unordered_map<std::string, std::vector<std::pair<size_t, size_t>>> surname_hits;
  for (size_t i = 0; i < all.size(); ++i) {
    const CandidateRef& c = all[i];
    if (c.surname.empty() || c.list) continue;
    auto it = surname_hits.find(c.surname);
    if (it == surname_hits.end()) {
      std::vector<std::pair<size_t, size_t>> hits;
      for (size_t p = FindSurnameWithTitle(text, c.surname); p != std::string::npos;
           p = FindSurnameWithTitle(text, c.surname, p + 1)) {
        hits.push_back({p, c.surname.size() + TitleAt(text, p + c.surname.size())});
      }
      it = surname_hits.emplace(c.surname, std::move(hits)).first;
    }
    if (it->second.empty()) continue;
    bool ok = c.unique_surname || ContainsRaceTitle(text, c.race_title);
    if (!ok && c.unique_in_party) {
      if (!parties_done) {
        parties = MentionedParties(a);
        parties_done = true;
      }
      ok = std::find(parties.begin(), parties.end(), c.party_code) != parties.end();
    }
    if (!ok) continue;
    for (const auto& [p, len] : it->second) occ.push_back({i, p, len});
  }
  // A name inside a longer name of someone else ("山本しん" in "山本しんじ")
  // does not count.
  std::vector<size_t> first(all.size(), std::string::npos);
  for (const Occ& o : occ) {
    bool covered = false;
    for (const Occ& other : occ) {
      if (other.ref == o.ref || other.len <= o.len) continue;
      if (other.pos <= o.pos && o.pos + o.len <= other.pos + other.len) {
        covered = true;
        break;
      }
    }
    if (!covered) first[o.ref] = std::min(first[o.ref], o.pos);
  }
  std::vector<size_t> order;
  for (size_t i = 0; i < all.size(); ++i) {
    if (first[i] != std::string::npos) order.push_back(i);
  }
  // In order of first mention: the subject of an article usually comes first.
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t x, size_t y) { return first[x] < first[y]; });
  std::vector<CandidateRef> out;
  for (size_t i : order) out.push_back(all[i]);
  return out;
}

bool HeuristicClassifier::Classify(const store::Article& a,
                                   const std::vector<CandidateRef>& candidates,
                                   ClassifyResult* out, std::string*) {
  out->model = Name();
  out->digest = a.title;
  out->assessments.clear();
  out->parties.clear();
  const std::vector<std::string> sentences = Sentences(ArticleText(a));

  using Finder = std::function<Match(const std::string&, const std::string&)>;
  std::vector<Finder> finders;
  for (const CandidateRef& c : candidates) {
    // Surname mentions only when no other assessed candidate shares it.
    const bool allow_surname =
        !c.surname.empty() &&
        std::none_of(candidates.begin(), candidates.end(), [&](const CandidateRef& o) {
          return o.id != c.id && o.surname == c.surname;
        });
    finders.push_back([&c, allow_surname](const std::string& s, const std::string& lower) {
      return FindName(s, lower, c, allow_surname);
    });
  }
  const std::vector<int> scores = ScoreEntities(sentences, finders);
  for (size_t i = 0; i < candidates.size(); ++i) {
    store::Assessment as;
    as.candidate_id = candidates[i].id;
    as.sentiment = FromScore(scores[i]);
    as.reason = ScoreReason(scores[i]);
    out->assessments.push_back(std::move(as));
  }

  const std::vector<std::string> parties = MentionedParties(a);
  std::vector<Finder> party_finders;
  for (const std::string& code : parties) {
    party_finders.push_back(
        [code](const std::string& s, const std::string&) { return FindParty(s, code); });
  }
  const std::vector<int> party_scores = ScoreEntities(sentences, party_finders);
  for (size_t i = 0; i < parties.size(); ++i) {
    out->parties.push_back({parties[i], FromScore(party_scores[i]), ScoreReason(party_scores[i])});
  }
  return true;
}

void ApplyLlmEnvironment(LlmConfig* config) {
  auto env = [](const char* name) -> std::string {
    const char* v = std::getenv(name);
    return v ? v : "";
  };
  if (config->api_key.empty()) config->api_key = env("JPY_LLM_API_KEY");
  if (config->api_key.empty()) config->api_key = env("OPENAI_API_KEY");
  // Environment only replaces defaults: explicit flags win.
  const LlmConfig defaults;
  if (config->base_url == defaults.base_url) {
    if (!env("JPY_LLM_BASE_URL").empty()) config->base_url = env("JPY_LLM_BASE_URL");
    else if (!env("OPENAI_BASE_URL").empty()) config->base_url = env("OPENAI_BASE_URL");
  }
  if (config->model == defaults.model && !env("JPY_LLM_MODEL").empty()) {
    config->model = env("JPY_LLM_MODEL");
  }
}

std::string LlmClassifier::BuildRequest(const LlmConfig& config, const store::Article& a,
                                        const std::vector<CandidateRef>& candidates) {
  std::string list;
  for (const CandidateRef& c : candidates) {
    std::string spellings;
    for (const std::string& n : c.names) {
      if (n == c.name) continue;
      if (c.list && spellings.size() > 120) {
        spellings += "…";
        break;
      }
      spellings += (spellings.empty() ? "" : "・") + n;
    }
    std::string name = c.name;
    if (!spellings.empty()) name += c.list ? "（名簿登載: " + spellings + "）" : "（" + spellings + "）";
    list += "- " + c.id + " | " + name + " | " + (c.name_en.empty() ? "-" : c.name_en) + " | " +
            c.party + " | " + c.race_title + "\n";
  }
  std::string party_list;
  for (const std::string& code : MentionedParties(a)) {
    const PartyEntry* p = PartyByCode(code);
    party_list += "- " + code + " | " + (p ? p->name : code) + "\n";
  }
  if (party_list.empty()) party_list = "(none)\n";
  const std::string system =
      "You analyse Japanese news about the 51st House of Representatives general election "
      "(第51回衆議院議員総選挙, 衆院選 2026; voting day 2026-02-08, polls close 20:00 JST). "
      "465 seats: 289 single-member districts (小選挙区) and 176 proportional seats (比例代表) "
      "in 11 blocs; district candidates may also stand on a party list (重複立候補) and win "
      "back a seat by 惜敗率 (比例復活). Main parties: 自民 (LDP, 高市早苗), 中道改革連合 (CRA, "
      "formed by 立憲民主党 and 公明党), 日本維新の会, 国民民主党, 参政党, 日本共産党, "
      "れいわ新選組, チームみらい, 日本保守党, 社民党, 減税日本・ゆうこく連合. For each listed "
      "candidate the article is about, decide whether the news is good, bad or neutral for "
      "that candidate's electoral prospects; do the same for each listed party the article "
      "discusses. Judge the facts reported (情勢調査, 出口調査, counts, 当選確実 calls, "
      "endorsements, gaffes, scandals), not the outlet's tone; routine campaign activity is "
      "neutral. An id starting with \"pr-\" stands for a party list's list-only candidates. "
      "Use only candidate ids and party codes from the lists. Reply with a single JSON "
      "object and nothing else:\n"
      "{\"summary\": \"<one sentence in Japanese, <= 60 characters>\", "
      "\"assessments\": [{\"candidate_id\": \"<id>\", \"sentiment\": \"good|bad|neutral\", "
      "\"reason\": \"<Japanese, <= 40 characters>\"}], "
      "\"parties\": [{\"party\": \"<code>\", \"sentiment\": \"good|bad|neutral\", "
      "\"reason\": \"<Japanese, <= 40 characters>\"}]}";
  const std::string user = "Candidates (id | 氏名 | name | 党派 | 選挙区):\n" + list +
                           "\nParties (code | 党名):\n" + party_list +
                           "\nArticle source: " + a.source + "\nPublished: " + a.published_at +
                           "\nTitle: " + a.title + "\nText: " + a.summary;
  json req = {{"model", config.model},
              {"temperature", config.temperature},
              {"messages",
               json::array({{{"role", "system"}, {"content", system}},
                            {{"role", "user"}, {"content", user}}})}};
  if (config.json_mode) req["response_format"] = {{"type", "json_object"}};
  // Feeds occasionally carry malformed UTF-8: replace it rather than throw.
  return req.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool LlmClassifier::ParseResponse(const std::string& body,
                                  const std::vector<CandidateRef>& candidates,
                                  ClassifyResult* out, std::string* error) {
  const json resp = json::parse(body, nullptr, false);
  if (resp.is_discarded()) {
    *error = "LLM response is not JSON";
    return false;
  }
  if (resp.contains("error")) {
    *error = "LLM error: " + resp["error"].dump();
    return false;
  }
  std::string content;
  try {
    content = resp.at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (const json::exception&) {
    *error = "unexpected LLM response shape";
    return false;
  }
  // Tolerate ```json fences and prose around the object.
  const size_t b = content.find('{'), e = content.rfind('}');
  if (b == std::string::npos || e == std::string::npos || e < b) {
    *error = "no JSON object in LLM answer";
    return false;
  }
  const json answer = json::parse(content.substr(b, e - b + 1), nullptr, false);
  if (answer.is_discarded() || !answer.is_object()) {
    *error = "LLM answer is not valid JSON";
    return false;
  }
  if (resp.contains("model") && resp["model"].is_string()) {
    out->model = resp["model"].get<std::string>();
  }
  auto str = [](const json& o, const char* key) {
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>()
                                                                  : std::string();
  };
  out->digest = str(answer, "summary");
  out->assessments.clear();
  out->parties.clear();
  const json items = answer.value("assessments", json::array());
  for (const json& item : items.is_array() ? items : json::array()) {
    const std::string id = str(item, "candidate_id");
    const bool known = std::any_of(candidates.begin(), candidates.end(),
                                   [&](const CandidateRef& c) { return c.id == id; });
    const bool dup = std::any_of(out->assessments.begin(), out->assessments.end(),
                                 [&](const store::Assessment& x) { return x.candidate_id == id; });
    store::Sentiment s;
    if (!known || dup || !store::ParseSentiment(Lower(str(item, "sentiment")), &s)) continue;
    out->assessments.push_back({id, s, str(item, "reason")});
  }
  const json parties = answer.value("parties", json::array());
  for (const json& item : parties.is_array() ? parties : json::array()) {
    const std::string code = str(item, "party");
    store::Sentiment s;
    if (!PartyByCode(code) || !store::ParseSentiment(Lower(str(item, "sentiment")), &s)) continue;
    out->parties.push_back({code, s, str(item, "reason")});
  }
  return true;
}

bool LlmClassifier::Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                             ClassifyResult* out, std::string* error) {
  out->model = config_.model;
  std::string url = config_.base_url;
  while (!url.empty() && url.back() == '/') url.pop_back();
  url += "/chat/completions";
  std::vector<std::string> headers;
  if (!config_.api_key.empty()) headers.push_back("Authorization: Bearer " + config_.api_key);
  const HttpResponse r =
      HttpPostJson(url, BuildRequest(config_, a, candidates), headers, config_.timeout_s);
  if (!r.error.empty()) {
    *error = "LLM request failed: " + r.error;
    return false;
  }
  if (r.status != 200) {
    *error = "LLM HTTP " + std::to_string(r.status) + ": " + r.body.substr(0, 300);
    return false;
  }
  return ParseResponse(r.body, candidates, out, error);
}

}  // namespace jpy::news
