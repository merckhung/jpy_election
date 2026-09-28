// Decides, for each candidate an article is about, whether the news is good,
// bad or neutral for that candidate (and, as a by-product, for each party it
// mentions).
//
//  * LlmClassifier talks to any OpenAI-compatible Chat Completions endpoint
//    (OpenAI, Azure OpenAI, OpenRouter, Ollama, vLLM, LM Studio, llama.cpp
//    server, ...): POST {base_url}/chat/completions, JSON-only answer.
//  * HeuristicClassifier is an offline keyword/lexicon fallback so counts
//    still work without an API key (marked model = "heuristic"). It knows
//    Japanese election vocabulary (優勢 / 苦戦 / 当選確実 / 落選 ...) and
//    Japanese word order ("AがBを逆転" is good for A, bad for B).
//
// Candidates are matched by every spelling the data knows: the ballot name
// (山田みき), the registered name (山田美樹), surname + given name, katakana
// reading and the romanised name; "surname + title" mentions (高市首相,
// 野田代表, 玉木氏) count when the surname is unambiguous, or the article
// names the candidate's district or party. Names that appear only on a PR
// list (not running in a district) map to that party list's id
// ("pr-chugoku-CRA").
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/store/db.h"

namespace jpy::news {

struct CandidateRef {
  // SMD candidate id ("13-01-02"), or a PR party-list id ("pr-tokyo-LDP")
  // standing for the list-only names on that list.
  std::string id;
  std::string name;                // display name: ballot name / "自民 比例東京ブロック名簿"
  std::vector<std::string> names;  // spellings matched in text (normalised)
  std::vector<std::string> aliases;  // leader titles that name this person ("高市首相")
  std::string name_en;             // "Miki Yamada" ("" for lists)
  std::string surname;             // kanji surname for "高市首相"-style mentions
  bool unique_surname = false;     // no other candidate shares the surname
  bool unique_in_party = false;    // no other candidate of the party shares it
  std::string party;               // party short name (自民)
  std::string party_code;          // "LDP"
  std::string race_title;          // "東京1区" / "比例東京ブロック"
  bool list = false;               // PR party-list ref
};

std::vector<CandidateRef> CandidateRefs(const election::ElectionData& data);

// Candidates whose name (in any known spelling) appears in the article.
std::vector<CandidateRef> MentionedCandidates(const store::Article& a,
                                              const std::vector<CandidateRef>& all);

// Parties mentioned by name, abbreviation or leader (自民 / 高市早苗 → LDP,
// 維新 → JIP, れいわ → REIWA, ...). Returns party codes, most mentioned first.
std::vector<std::string> MentionedParties(const store::Article& a);
// Built-in keyword table of a party code (empty for unknown codes).
const std::vector<std::string>& PartyKeywords(const std::string& code);

// Full-width ASCII -> ASCII, ideographic space -> space, 髙 -> 高, 﨑 -> 崎.
std::string NormalizeText(const std::string& s);

struct PartyAssessment {
  std::string party;  // party code
  store::Sentiment sentiment = store::Sentiment::kNeutral;
  std::string reason;
};

struct ClassifyResult {
  std::string digest;  // one-sentence summary
  std::string model;
  std::vector<store::Assessment> assessments;
  // Party-level sentiment (informational; the store keys news by candidate).
  std::vector<PartyAssessment> parties;
};

class Classifier {
 public:
  virtual ~Classifier() = default;
  // `candidates` are the (pre-filtered) candidates to assess.
  virtual bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                        ClassifyResult* out, std::string* error) = 0;
  virtual std::string Name() const = 0;
};

class HeuristicClassifier : public Classifier {
 public:
  bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                ClassifyResult* out, std::string* error) override;
  std::string Name() const override { return "heuristic"; }
};

struct LlmConfig {
  std::string base_url = "https://api.openai.com/v1";
  std::string model = "gpt-4o-mini";
  std::string api_key;  // from $JPY_LLM_API_KEY or $OPENAI_API_KEY when empty
  double temperature = 0;
  bool json_mode = true;  // send response_format {"type": "json_object"}
  long timeout_s = 60;
};

// Fills api_key/base_url/model from the environment (JPY_LLM_API_KEY or
// OPENAI_API_KEY, JPY_LLM_BASE_URL or OPENAI_BASE_URL, JPY_LLM_MODEL) when
// they are not already set on the command line.
void ApplyLlmEnvironment(LlmConfig* config);

class LlmClassifier : public Classifier {
 public:
  explicit LlmClassifier(LlmConfig config) : config_(std::move(config)) {}
  bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                ClassifyResult* out, std::string* error) override;
  std::string Name() const override { return config_.model; }

  // Exposed for tests.
  static std::string BuildRequest(const LlmConfig& config, const store::Article& a,
                                  const std::vector<CandidateRef>& candidates);
  static bool ParseResponse(const std::string& body, const std::vector<CandidateRef>& candidates,
                            ClassifyResult* out, std::string* error);

 private:
  LlmConfig config_;
};

}  // namespace jpy::news
