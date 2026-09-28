// UI localisation: Japanese (default), English, Traditional Chinese.
//
// UI strings live in a key -> {ja, en, zh-TW} table (i18n.cc plus the
// per-component string lists i18n_*.inc). Data names come from the data
// files: Japanese names as published (ballot names, kana readings), English
// romanisations and Traditional Chinese renderings prepared by the data
// pipeline.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace jpy::ui {

enum class Lang { kJa = 0, kEn = 1, kZhTW = 2, kCount = 3 };

// Accepts "ja", "jp", "ja-JP", "en", "en-US", "zh", "zh-TW", "tw", ...
// (case-insensitive).
bool ParseLang(std::string_view s, Lang* out);
const char* LangCode(Lang l);        // "ja", "en", "zh-TW"
const char* LangNativeName(Lang l);  // "日本語", "English", "繁體中文"
Lang NextLang(Lang l);               // ja -> en -> zh-TW -> ja

// Looks up a UI string; falls back to Japanese, then the key.
const char* Tr(Lang lang, std::string_view key);
// True if `key` exists in the table (tests).
bool HasKey(std::string_view key);
// Every key in the table (tests check each has all three languages).
std::vector<std::string_view> AllKeys();

// Substitutes {0}, {1}, ... in `pattern`.
std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args);

class Localizer {
 public:
  explicit Localizer(Lang lang = Lang::kJa) : lang_(lang) {}
  Lang lang() const { return lang_; }
  const char* T(std::string_view key) const { return Tr(lang_, key); }

  // "東京都" / "Tokyo" / "東京都"; districts "東京1区" / "Tokyo 1st" / "東京第1區".
  std::string RegionName(const geo::Region& r) const;
  // Ballot name "山田みき" / "Miki Yamada" / "山田美樹" (zh falls back to ja).
  std::string CandidateName(const election::Candidate& c) const;
  // Secondary line under the name: kana (ja), Japanese name (en), kana (zh).
  std::string CandidateAltName(const election::Candidate& c) const;
  // A PR list entry (list-only names have no Candidate record).
  std::string ListEntryName(const election::ListEntry& e) const;
  std::string PartyShort(const election::Party& p) const;
  std::string PartyName(const election::Party& p) const;
  // "東京1区" / "Tokyo 1st district" / "東京第1區"; "比例東京ブロック" / ...
  std::string RaceTitle(const election::Race& r) const;
  std::string BlocName(const election::Bloc& b) const;
  std::string OfficeName(const election::OfficeSummary& o) const;
  std::string ElectionName(const election::ElectionInfo& info) const;
  // National review: justice name / question / short biography.
  std::string Justice(const election::Referendum& r) const;
  std::string Question(const election::Referendum& r) const;
  std::string Bio(const election::Referendum& r) const;
  // 新 / 前 / 元 -> "新" "前" "元" / "New" "Inc." "Former" / "新" "現任" "前任".
  std::string Status(const std::string& status_ja) const;
  std::string ListSeparator() const;  // "、" or ", "

 private:
  Lang lang_;
};

}  // namespace jpy::ui
