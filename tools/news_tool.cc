// news_tool: ニュース・パイプラインと SQLite ストアのコマンドライン操作。
// Command-line access to the news pipeline and its SQLite store.
//
//   news_tool --fetch                 data/news/feeds.json を1回取得して分類 / fetch once and classify
//   news_tool --ingest=FILE.jsonl     記事を追加 (1行1 JSON) / add articles (one JSON object per line)
//   news_tool --classify              未分類の記事を分類 / classify pending articles
//   news_tool --text="見出し…"         1件をその場で分類 (DB 不使用) / classify one text offline
//   news_tool --stats                 候補者別の好材料/悪材料/中立 / good/bad/neutral per candidate
//   news_tool --latest=N              最新の分類済み記事 / latest classified articles
//   news_tool --mock=N --out=F.jsonl  模擬記事 N 件を出力 (または --rss=F.xml) / N labelled mock articles
//   news_tool --eval=N                模擬記事 N 件で分類精度を評価 / classifier accuracy on N mocks
// 共通 / common: --db=FILE (既定 <root>/jpy_election.db), --root=DIR,
//         --llm_base_url=URL --llm_model=NAME --no_llm  (キー / key: $OPENAI_API_KEY)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "nlohmann/json.hpp"
#include "src/election/model.h"
#include "src/news/classifier.h"
#include "src/news/feed_parser.h"
#include "src/news/mock_news.h"
#include "src/news/service.h"
#include "src/store/db.h"
#include "tools/cli_utf8.h"

using namespace jpy;

namespace {

constexpr char kUsage[] =
    "使い方: news_tool [オプション]\n"
    "  --fetch                 feeds.json のフィードを取得して分類\n"
    "  --ingest=FILE.jsonl     JSONL の記事を追加\n"
    "  --classify              未分類の記事を分類\n"
    "  --text=\"見出し…\"        1件をその場で分類 (DB 不使用)\n"
    "  --stats                 候補者別の好材料・悪材料・中立\n"
    "  --latest=N              最新の分類済み記事 N 件\n"
    "  --mock=N [--out=F.jsonl | --rss=F.xml]  模擬記事 (【模擬】) を N 件出力\n"
    "  --eval=N                模擬記事 N 件で分類精度を評価\n"
    "  共通: --db=FILE --root=DIR --llm_base_url=URL --llm_model=NAME --no_llm\n"
    "        (API キー: $JPY_LLM_API_KEY または $OPENAI_API_KEY)\n"
    "\n"
    "usage: news_tool [flags]\n"
    "  --fetch                 fetch the feeds in feeds.json and classify\n"
    "  --ingest=FILE.jsonl     add JSONL articles\n"
    "  --classify              classify pending articles\n"
    "  --text=\"headline...\"    classify one text offline (no DB)\n"
    "  --stats                 good/bad/neutral per candidate\n"
    "  --latest=N              latest N classified articles\n"
    "  --mock=N [--out=F.jsonl | --rss=F.xml]  write N labelled mock articles\n"
    "  --eval=N                classifier accuracy on N mock articles\n"
    "  common: --db=FILE --root=DIR --llm_base_url=URL --llm_model=NAME --no_llm\n"
    "        (API key: $JPY_LLM_API_KEY or $OPENAI_API_KEY)\n";

std::string XmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

std::string Dump(const nlohmann::json& j) {
  return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// "高市早苗 (奈良2区)" for an assessment id (SMD candidate or party list).
std::string Describe(const election::ElectionData& data, const std::string& id) {
  const election::Race* race = nullptr;
  const election::Candidate* c = data.CandidateById(id, &race);
  if (!c || !race) return id;
  if (race->is_pr()) return data.party(c->party).short_ja + " " + race->name_ja + "名簿";
  return c->name_ja + " (" + data.party(c->party).short_ja + ", " + race->name_ja + ")";
}

}  // namespace

int main(int argc, char** argv) {
  std::string root = ".";
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) root = ws;
  std::string db_path, ingest, out, rss, text;
  bool fetch = false, classify = false, stats = false, no_llm = false, has_text = false;
  int latest = 0, mock = 0, eval = 0;
  news::LlmConfig llm;
  const std::vector<std::string> args = tools::Utf8Args(argc, argv);
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto val = [&](const char* flag) { return a.substr(std::strlen(flag)); };
    if (a.rfind("--root=", 0) == 0) root = val("--root=");
    else if (a.rfind("--db=", 0) == 0) db_path = val("--db=");
    else if (a == "--fetch") fetch = true;
    else if (a.rfind("--ingest=", 0) == 0) ingest = val("--ingest=");
    else if (a == "--classify") classify = true;
    else if (a.rfind("--text=", 0) == 0) text = val("--text="), has_text = true;
    else if (a == "--stats") stats = true;
    else if (a.rfind("--latest=", 0) == 0) latest = std::atoi(val("--latest=").c_str());
    else if (a.rfind("--mock=", 0) == 0) mock = std::atoi(val("--mock=").c_str());
    else if (a.rfind("--out=", 0) == 0) out = val("--out=");
    else if (a.rfind("--rss=", 0) == 0) rss = val("--rss=");
    else if (a.rfind("--eval=", 0) == 0) eval = std::atoi(val("--eval=").c_str());
    else if (a.rfind("--llm_base_url=", 0) == 0) llm.base_url = val("--llm_base_url=");
    else if (a.rfind("--llm_model=", 0) == 0) llm.model = val("--llm_model=");
    else if (a == "--no_llm") no_llm = true;
    else if (a == "--help" || a == "-h") {
      std::printf("%s", kUsage);
      return 0;
    } else {
      std::fprintf(stderr, "不明なオプション / unknown flag: %s\n\n%s", a.c_str(), kUsage);
      return 2;
    }
  }
  if (argc < 2) {
    std::fprintf(stderr, "%s", kUsage);
    return 2;
  }
  election::ElectionData data;
  std::string error;
  if (!data.Load(root + "/data/election/2026", &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const std::vector<news::CandidateRef> refs = news::CandidateRefs(data);

  if (mock > 0) {
    news::MockNewsGenerator gen(&data, 2026);
    const auto items = gen.Batch(mock);
    if (!rss.empty()) {
      std::ofstream f(rss, std::ios::binary);
      f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\"><channel>"
           "<title>jpy_election 模擬ニュース (MOCK)</title>\n";
      for (const auto& m : items) {
        f << "<item><title>" << XmlEscape(m.article.title) << "</title><link>" << m.article.url
          << "</link><pubDate>" << m.article.published_at << "</pubDate><description><![CDATA["
          << m.article.summary << "]]></description></item>\n";
      }
      f << "</channel></rss>\n";
      std::printf("模擬記事 %zu 件を書き出しました / wrote %zu mock items to %s\n", items.size(),
                  items.size(), rss.c_str());
    } else {
      std::ofstream file;
      if (!out.empty()) file.open(out, std::ios::binary);
      std::ostream& o = out.empty() ? std::cout : file;
      for (const auto& m : items) {
        o << Dump({{"title", m.article.title}, {"url", m.article.url},
                   {"source", m.article.source}, {"published", m.article.published_at},
                   {"summary", m.article.summary},
                   {"intended", {{"candidate_id", m.candidate_id},
                                 {"sentiment", store::SentimentName(m.intended)}}}})
          << "\n";
      }
    }
    return 0;
  }

  news::ApplyLlmEnvironment(&llm);
  std::unique_ptr<news::Classifier> classifier;
  if (!no_llm && (!llm.api_key.empty() || llm.base_url.find("127.0.0.1") != std::string::npos ||
                  llm.base_url.find("localhost") != std::string::npos)) {
    classifier = std::make_unique<news::LlmClassifier>(llm);
  } else {
    classifier = std::make_unique<news::HeuristicClassifier>();
  }

  if (has_text) {
    store::Article a;
    a.title = text;
    a.source = "news_tool";
    const auto mentioned = news::MentionedCandidates(a, refs);
    std::printf("分類器 / classifier: %s\n", classifier->Name().c_str());
    std::printf("言及された候補 / candidates:");
    for (const auto& c : mentioned) std::printf(" %s[%s]", c.name.c_str(), c.id.c_str());
    std::printf("\n言及された政党 / parties:");
    for (const auto& p : news::MentionedParties(a)) std::printf(" %s", p.c_str());
    std::printf("\n");
    news::ClassifyResult r;
    if (!classifier->Classify(a, mentioned, &r, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    if (!r.digest.empty()) std::printf("要約 / digest: %s\n", r.digest.c_str());
    for (const auto& as : r.assessments) {
      std::printf("  %-8s %s  %s\n", store::SentimentName(as.sentiment),
                  Describe(data, as.candidate_id).c_str(), as.reason.c_str());
    }
    for (const auto& p : r.parties) {
      std::printf("  %-8s [%s] %s  %s\n", store::SentimentName(p.sentiment), p.party.c_str(),
                  data.party(p.party).short_ja.c_str(), p.reason.c_str());
    }
    return 0;
  }

  if (eval > 0) {
    news::MockNewsGenerator gen(&data, 777);
    int correct = 0, total = 0, errors = 0;
    std::map<std::string, std::map<std::string, int>> confusion;
    for (const auto& m : gen.Batch(eval)) {
      news::ClassifyResult r;
      const auto mentioned = news::MentionedCandidates(m.article, refs);
      if (!classifier->Classify(m.article, mentioned, &r, &error)) {
        ++errors;
        continue;
      }
      std::string got = "missing";
      for (const auto& a : r.assessments) {
        if (a.candidate_id == m.candidate_id) got = store::SentimentName(a.sentiment);
      }
      const std::string want = store::SentimentName(m.intended);
      confusion[want][got]++;
      correct += got == want;
      ++total;
    }
    std::printf("分類器 / classifier: %s\n正解率 / accuracy: %d/%d (%.1f%%), エラー / errors: %d\n",
                classifier->Name().c_str(), correct, total, total ? 100.0 * correct / total : 0.0,
                errors);
    for (const auto& [want, row] : confusion) {
      std::printf("  intended %-8s ->", want.c_str());
      for (const auto& [got, n] : row) std::printf("  %s:%d", got.c_str(), n);
      std::printf("\n");
    }
    return 0;
  }

  store::Database db;
  if (db_path.empty()) db_path = root + "/jpy_election.db";
  if (!db.Open(db_path, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (!ingest.empty()) {
    std::string content;
    if (!election::ReadFile(ingest, &content)) {
      std::fprintf(stderr, "読み込めません / cannot read %s\n", ingest.c_str());
      return 1;
    }
    int added = 0;
    for (const auto& a : news::ParseJsonLines(content, &error)) {
      bool inserted = false;
      db.UpsertArticle(a, &inserted);
      added += inserted;
    }
    std::printf("新規記事 %d 件を追加 / ingested %d new articles\n", added, added);
  }
  if (fetch || classify) {
    news::NewsConfig config;
    config.db_path = db_path;
    config.llm = llm;
    config.use_llm = !no_llm;
    if (fetch && !news::LoadNewsConfig(root + "/data/news/feeds.json", data, &config, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
    }
    config.classify_per_cycle = 1000;
    news::NewsService service(config, refs);
    service.RunOnce(&db);
    std::printf("%s\n", service.Status().c_str());
  }
  if (stats) {
    const auto counts = db.CountsByCandidate();
    const store::SentimentCount t = db.TotalCounts();
    std::printf("合計 / total: 好材料 good %d, 悪材料 bad %d, 中立 neutral %d\n", t.good, t.bad,
                t.neutral);
    for (const auto& race : data.races()) {
      for (const auto& c : race.candidates) {
        auto it = counts.find(c.id);
        if (it == counts.end()) continue;
        const std::string name = race.is_pr() ? data.party(c.party).short_ja + " 名簿" : c.name_ja;
        std::printf("%-16s %-14s %-18s 好 %3d  悪 %3d  中 %3d  差 %+d\n", c.id.c_str(), name.c_str(),
                    race.name_ja.c_str(), it->second.good, it->second.bad, it->second.neutral,
                    it->second.good - it->second.bad);
      }
    }
  }
  if (latest > 0) {
    for (const auto& n : db.LatestNews(latest)) {
      std::printf("[%s] %s (%s)\n", n.model.c_str(), n.article.title.c_str(),
                  n.article.source.c_str());
      for (const auto& a : n.assessments) {
        std::printf("    %-8s %s  %s\n", store::SentimentName(a.sentiment),
                    Describe(data, a.candidate_id).c_str(), a.reason.c_str());
      }
    }
  }
  return 0;
}
