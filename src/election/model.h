// Static election data: parties, races, candidates, party lists, national
// review of Supreme Court justices, offices. Loaded from
// data/election/2026/{parties,candidates,election}.json.
//
// Japan's House of Representatives has 465 seats: 289 single-member
// districts (小選挙区, one `Race` of kind kSmd each) and 176 proportional
// seats in 11 regional blocs (比例代表, one `Race` of kind kPr each). In a PR
// race the "candidates" are the party lists (id "pr-tokyo-LDP"); each list
// carries its ranked entries in `Candidate::list`.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jpy::election {

struct Party {
  std::string code;  // "LDP", "CRA", "JIP", ..., "IND"
  std::string name_zh;
  std::string name_en;
  std::string short_zh;
  std::string name_ja;
  std::string short_ja;
  std::string short_en;
  uint32_t color = 0xFF8C8C8C;  // ARGB
};

// One entry of a PR party list (名簿).
struct ListEntry {
  int no = 0;  // list rank (名簿順位); dual candidates often share a rank
  std::string name_ja;
  std::string surname_ja;
  std::string given_ja;
  std::string smd;        // district code when also running in a district
  std::string candidate;  // that SMD candidate's id ("" when list-only)
  int order = 0;          // official PR election order (0 = not elected)
  bool disqualified = false;  // official: struck from the list (see seats.h)
};

struct Candidate {
  std::string id;  // SMD: "13-01-02"; PR list: "pr-tokyo-LDP"
  std::string name_ja;     // ballot name (may use kana, e.g. 山田みき)
  std::string surname_ja;
  std::string given_ja;
  std::string name_legal;  // registered name (戸籍), e.g. 山田美樹
  std::string kana;        // "ヤマダ ミキ"
  std::string name_en;
  std::string name_zh;
  bool romanization_guess = false;
  std::string party;  // party code
  std::string party_ja;
  int age = 0;
  std::string status;  // 新 (new) / 前 (incumbent) / 元 (former member)
  std::string occupation;
  bool dual = false;               // 重複立候補: also on a PR list
  bool dual_disqualified = false;  // official: < 10% of valid votes (list seat barred)
  double sekihairitsu = 0;         // official 惜敗率 (%), 100 for winners
  bool incumbent = false;          // status == 前
  bool pr_elected = false;         // official: 比例復活
  std::string photo;  // path relative to the data root (may not exist)
  std::string note;

  // PR party lists only.
  int seats_won = -1;  // official seats won in the bloc (-1 = unknown)
  std::vector<ListEntry> list;
};

struct Incumbent {
  std::string name_ja;
  std::string party;
  bool running = false;
  std::string note;
};

enum class RaceKind { kSmd, kPr };

struct Race {
  std::string id;      // "13-01" / "pr-tokyo"
  RaceKind kind = RaceKind::kSmd;
  std::string region;  // SMD: district code; PR: "JP"
  std::string pref;    // SMD: prefecture code
  std::string bloc;    // PR bloc id ("tokyo"); SMDs name their bloc too
  std::vector<std::string> prefs;  // PR: prefectures of the bloc
  int seats = 1;
  std::string name_ja;  // "東京1区" / "比例東京ブロック"
  std::string name_en;
  std::string name_zh;
  Incumbent incumbent;  // SMD: 2024 winner
  std::vector<Candidate> candidates;

  bool is_pr() const { return kind == RaceKind::kPr; }
  bool is_smd() const { return kind == RaceKind::kSmd; }
  int CandidateIndex(std::string_view candidate_id) const;
  // True when the race's ballots are cast in prefecture `pref`.
  bool CoversPref(std::string_view pref) const;
};

// National review of Supreme Court justices (最高裁判所裁判官国民審査); one
// entry per justice. Kept under the old name `Referendum` to limit churn.
struct Referendum {
  std::string id;    // "review-takasu"
  std::string kind;  // "review"
  std::string justice_ja, justice_en, justice_zh;
  std::string question_ja, question_en, question_zh;
  std::string bio_ja, bio_en, bio_zh;
  std::string note;
};

struct Bloc {
  std::string id;  // "tokyo"
  std::string name_ja, name_en, name_zh;
  int seats = 0;
  std::vector<std::string> prefs;
};

struct OfficeSummary {
  std::string name_zh;
  std::string name_ja;
  std::string name_en;
  std::string kind;  // "smd" / "pr"
  int seats = 0;
  int candidates = 0;
  bool headline = false;
};

struct ElectionInfo {
  std::string id;
  std::string date;  // YYYY-MM-DD
  std::string name_ja;
  std::string name_zh;
  std::string name_en;
  std::string polls_open;   // "07:00" local (JST)
  std::string polls_close;  // "20:00"
  std::string timezone;     // "Asia/Tokyo"
  std::string dissolution;  // YYYY-MM-DD
  std::string status_note;
  std::vector<OfficeSummary> offices;
  int total_seats = 0;      // 465
  int majority = 0;         // 233
  int supermajority = 0;    // 310 (two thirds)
  int total_candidates = 0;
  std::vector<Bloc> blocs;
  std::vector<Referendum> referendums;  // national review, one per justice
};

class ElectionData {
 public:
  // Loads the three JSON files from `dir` (e.g. "data/election/2026").
  bool Load(const std::string& dir, std::string* error);

  // Same, from in-memory JSON text (used by tests).
  bool LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                    std::string_view election_json, std::string* error);

  const ElectionInfo& info() const { return info_; }
  const std::vector<Party>& parties() const { return parties_; }
  const std::vector<Race>& races() const { return races_; }

  const Party& party(std::string_view code) const;  // falls back to IND
  const Race* RaceById(std::string_view id) const;
  // SMD race of a district code ("13-01"), or nullptr.
  const Race* SmdRaceForDistrict(std::string_view district_code) const;
  // PR race of the bloc containing prefecture `pref`, or nullptr.
  const Race* PrRaceForPref(std::string_view pref) const;
  const Race* PrRaceForBloc(std::string_view bloc_id) const;
  const Bloc* BlocById(std::string_view bloc_id) const;
  const Bloc* BlocForPref(std::string_view pref) const;
  const Candidate* CandidateById(std::string_view id, const Race** race = nullptr) const;

 private:
  ElectionInfo info_;
  std::vector<Party> parties_;
  std::vector<Race> races_;
  std::unordered_map<std::string, int> race_by_id_;
  std::unordered_map<std::string, int> smd_by_district_;
  std::unordered_map<std::string, int> pr_by_pref_;
  Party unknown_party_;
};

bool ReadFile(const std::string& path, std::string* out);

// Parses "#RRGGBB" to 0xFFRRGGBB.
uint32_t ParseColor(std::string_view hex, uint32_t fallback);

}  // namespace jpy::election
