#include <set>
#include <string>

#include "gtest/gtest.h"
#include "src/ui/i18n.h"

namespace jpy::ui {
namespace {

TEST(I18n, DefaultsToJapanese) {
  Localizer l;
  EXPECT_EQ(l.lang(), Lang::kJa);
  EXPECT_STREQ(l.T("status.counting"), "開票中");
  EXPECT_STREQ(l.T("ev.called.title"), "当選確実");
  EXPECT_STREQ(LangCode(Lang{}), "ja");  // value-initialised Lang is Japanese
  EXPECT_STREQ(LangNativeName(Lang::kJa), "日本語");
}

TEST(I18n, FallsBackToJapanese) {
  // Out-of-range languages read the Japanese column.
  EXPECT_STREQ(Tr(static_cast<Lang>(7), "chart.map"), "地図");
  EXPECT_STREQ(Tr(Lang::kEn, "no.such.key"), "no.such.key");
}

TEST(I18n, ParsesLanguageFlags) {
  Lang l = Lang::kJa;
  ASSERT_TRUE(ParseLang("EN", &l));
  EXPECT_EQ(l, Lang::kEn);
  ASSERT_TRUE(ParseLang("zh-TW", &l));
  EXPECT_EQ(l, Lang::kZhTW);
  ASSERT_TRUE(ParseLang("ja", &l));
  EXPECT_EQ(l, Lang::kJa);
  ASSERT_TRUE(ParseLang("JP", &l));
  EXPECT_EQ(l, Lang::kJa);
  EXPECT_FALSE(ParseLang("fr", &l));
  EXPECT_EQ(NextLang(Lang::kJa), Lang::kEn);
  EXPECT_EQ(NextLang(Lang::kEn), Lang::kZhTW);
  EXPECT_EQ(NextLang(Lang::kZhTW), Lang::kJa);
}

TEST(I18n, CoreKeysTranslatedInAllLanguages) {
  const char* keys[] = {"app.title",       "window.title",       "status.countdown",
                        "footer.controls", "help.9",             "level.village",
                        "chart.trend",     "chart.seats",        "chart.margins",
                        "chart.parties",   "chart.grid",         "pip.title",
                        "ev.lead_change.text", "ev.victory.text", "ev.called.text",
                        "news.totals",     "mode.review",        "review.title",
                        "seats.majority",  "status.replay",      "status.simulation"};
  for (const char* k : keys) {
    ASSERT_TRUE(HasKey(k)) << k;
    std::set<std::string> distinct;
    for (Lang l : {Lang::kJa, Lang::kEn, Lang::kZhTW}) distinct.insert(Tr(l, k));
    EXPECT_GE(distinct.size(), 2u) << k;  // at least English differs from CJK
  }
}

TEST(I18n, EveryEntryHasAllThreeLanguages) {
  const auto keys = AllKeys();
  EXPECT_GT(keys.size(), 100u);
  for (std::string_view k : keys) {
    for (Lang l : {Lang::kJa, Lang::kEn, Lang::kZhTW}) {
      const std::string s = Tr(l, k);
      EXPECT_FALSE(s.empty()) << k;
    }
    // English strings contain no kana (catches untranslated copies).
    const std::string en = Tr(Lang::kEn, k);
    EXPECT_EQ(en.find("\xE3\x81"), std::string::npos) << k << ": " << en;  // U+3040-307F
  }
}

TEST(I18n, Formats) {
  EXPECT_EQ(Fmt("投票日まで {0} 日", {"3"}), "投票日まで 3 日");
  EXPECT_EQ(Fmt("{1}/{0}", {"a", "b"}), "b/a");
  EXPECT_EQ(Fmt("{5}", {"a"}), "");
}

TEST(I18n, LocalizedNames) {
  election::Race race;
  race.name_ja = "東京1区";
  race.name_en = "Tokyo 1st district";
  race.name_zh = "東京第1區";
  EXPECT_EQ(Localizer().RaceTitle(race), "東京1区");
  EXPECT_EQ(Localizer(Lang::kEn).RaceTitle(race), "Tokyo 1st district");
  EXPECT_EQ(Localizer(Lang::kZhTW).RaceTitle(race), "東京第1區");

  election::Candidate c;
  c.name_ja = "山田みき";
  c.kana = "ヤマダ ミキ";
  c.name_en = "Miki Yamada";
  EXPECT_EQ(Localizer().CandidateName(c), "山田みき");
  EXPECT_EQ(Localizer().CandidateAltName(c), "ヤマダ ミキ");
  EXPECT_EQ(Localizer(Lang::kEn).CandidateName(c), "Miki Yamada");
  EXPECT_EQ(Localizer(Lang::kEn).CandidateAltName(c), "山田みき");
  // No Chinese rendering: Traditional Chinese shows the Japanese name.
  EXPECT_EQ(Localizer(Lang::kZhTW).CandidateName(c), "山田みき");

  geo::Region r;
  r.level = geo::Level::kNation;
  EXPECT_EQ(Localizer().RegionName(r), "日本");
  EXPECT_EQ(Localizer(Lang::kEn).RegionName(r), "Japan");
  r.level = geo::Level::kCounty;
  r.name_ja = "東京都";
  r.name_en = "Tokyo";
  r.name_zh = "東京都";
  EXPECT_EQ(Localizer().RegionName(r), "東京都");
  EXPECT_EQ(Localizer(Lang::kEn).RegionName(r), "Tokyo");

  EXPECT_EQ(Localizer().Status("前"), "前");
  EXPECT_EQ(Localizer(Lang::kEn).Status("元"), "Former");
  EXPECT_EQ(Localizer(Lang::kZhTW).Status("前"), "現任");
}

}  // namespace
}  // namespace jpy::ui
