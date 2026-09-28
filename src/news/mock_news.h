// Synthetic election news for testing the news pipeline, the LLM classifier
// and the UI. Every item is labelled "【模擬】" (source "MOCK", url mock://...)
// and says it is not a real report (実際の報道ではありません). Templates are
// deliberately mild (情勢調査 / 出口調査 moves, crowds at speeches, criticism
// by rivals, the mood at campaign offices on counting night): no invented
// crimes or scandals about real people. Each item carries the sentiment it
// was written to express, so classifier accuracy can be measured
// (news_tool --eval).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/store/db.h"

namespace jpy::news {

struct MockArticle {
  store::Article article;
  std::string candidate_id;      // subject of the item (an SMD candidate)
  store::Sentiment intended;     // ground truth for the subject
};

class MockNewsGenerator {
 public:
  MockNewsGenerator(const election::ElectionData* data, uint64_t seed);

  // Items "published" in the half-open interval of simulated minutes after
  // the close of polls (20:00 JST on election day; negative = earlier that
  // day, > 240 = after midnight). ~`per_hour` items per simulated hour on
  // average. Before 20:00 the items are about the campaign's final polls;
  // afterwards about exit polls and the count.
  std::vector<MockArticle> Between(double from_minute, double to_minute, double per_hour = 12);

  // `n` items (for tests / evaluation), independent of time, drawn from all
  // templates.
  std::vector<MockArticle> Batch(int n);

 private:
  // `phase`: 0 = before the close of polls, 1 = counting night, -1 = any.
  MockArticle Make(uint64_t h, double minute, int phase);

  const election::ElectionData* data_;
  uint64_t seed_;
  std::vector<std::pair<int, int>> pool_;  // (race, candidate) weighted by prominence
};

}  // namespace jpy::news
