#include "src/election/model.h"

#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace jpy::election {

using nlohmann::json;

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

uint32_t ParseColor(std::string_view hex, uint32_t fallback) {
  if (hex.size() != 7 || hex[0] != '#') return fallback;
  uint32_t v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = hex[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return fallback;
  }
  return 0xFF000000u | v;
}

int Race::CandidateIndex(std::string_view candidate_id) const {
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].id == candidate_id) return static_cast<int>(i);
  }
  return -1;
}

bool Race::CoversPref(std::string_view p) const {
  if (kind == RaceKind::kSmd) return pref == p;
  for (const std::string& q : prefs) {
    if (q == p) return true;
  }
  return false;
}

bool ElectionData::Load(const std::string& dir, std::string* error) {
  std::string parties, candidates, election;
  for (auto [name, dst] : {std::pair{"parties.json", &parties},
                           std::pair{"candidates.json", &candidates},
                           std::pair{"election.json", &election}}) {
    if (!ReadFile(dir + "/" + name, dst)) {
      *error = "cannot read " + dir + "/" + name;
      return false;
    }
  }
  return LoadFromJson(parties, candidates, election, error);
}

namespace {

// Null-tolerant accessors (the data uses JSON null for "not applicable").
std::string Str(const json& j, const char* key, const std::string& fallback = "") {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

int Int(const json& j, const char* key, int fallback = 0) {
  auto it = j.find(key);
  return it != j.end() && it->is_number() ? static_cast<int>(it->get<double>()) : fallback;
}

double Num(const json& j, const char* key, double fallback = 0) {
  auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

bool Bool(const json& j, const char* key, bool fallback = false) {
  auto it = j.find(key);
  return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::vector<std::string> Strings(const json& j, const char* key) {
  std::vector<std::string> out;
  auto it = j.find(key);
  if (it == j.end() || !it->is_array()) return out;
  for (const json& v : *it) {
    if (v.is_string()) out.push_back(v.get<std::string>());
  }
  return out;
}

}  // namespace

bool ElectionData::LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                                std::string_view election_json, std::string* error) {
  const json pj = json::parse(parties_json, nullptr, false);
  const json cj = json::parse(candidates_json, nullptr, false);
  const json ej = json::parse(election_json, nullptr, false);
  if (pj.is_discarded() || cj.is_discarded() || ej.is_discarded()) {
    *error = "invalid election JSON";
    return false;
  }
  try {
    parties_.clear();
    for (const json& p : pj.at("parties")) {
      Party party;
      party.code = p.at("code").get<std::string>();
      party.name_ja = Str(p, "name_ja", party.code);
      party.short_ja = Str(p, "short_ja", party.name_ja);
      party.name_en = Str(p, "name_en", party.code);
      party.short_en = Str(p, "short_en", party.code);
      party.name_zh = Str(p, "name_zh", party.name_ja);
      party.short_zh = Str(p, "short_zh", party.short_ja);
      party.color = ParseColor(Str(p, "color"), 0xFF8C8C8C);
      parties_.push_back(std::move(party));
    }
    unknown_party_ = Party{"IND", "無黨籍", "Independent", "無黨籍", "無所属", "無所属", "Ind.",
                           0xFF8C8C8C};

    const json& e = cj.at("election");
    info_.id = Str(e, "id");
    info_.date = Str(e, "date");
    info_.name_ja = Str(e, "name_ja");
    info_.name_zh = Str(e, "name_zh", info_.name_ja);
    info_.name_en = Str(e, "name_en");
    info_.polls_open = Str(e, "polls_open", "07:00");
    info_.polls_close = Str(e, "polls_close", "20:00");
    info_.timezone = Str(e, "timezone", "Asia/Tokyo");
    info_.status_note = Str(cj, "status_note");

    races_.clear();
    for (const json& r : cj.at("races")) {
      Race race;
      race.id = r.at("id").get<std::string>();
      race.kind = Str(r, "kind", "smd") == "pr" ? RaceKind::kPr : RaceKind::kSmd;
      race.region = Str(r, "region");
      race.pref = Str(r, "pref");
      race.bloc = Str(r, "bloc");
      race.prefs = Strings(r, "prefs");
      race.seats = Int(r, "seats", 1);
      race.name_ja = Str(r, "name_ja", race.id);
      race.name_en = Str(r, "name_en", race.name_ja);
      race.name_zh = Str(r, "name_zh", race.name_ja);
      if (auto it = r.find("incumbent"); it != r.end() && it->is_object()) {
        race.incumbent.name_ja = Str(*it, "name_ja");
        race.incumbent.party = Str(*it, "party");
        race.incumbent.running = Bool(*it, "running");
        race.incumbent.note = Str(*it, "note");
      }
      for (const json& c : r.at("candidates")) {
        Candidate cand;
        cand.id = c.at("id").get<std::string>();
        cand.name_ja = Str(c, "name_ja");
        cand.surname_ja = Str(c, "surname_ja");
        cand.given_ja = Str(c, "given_ja");
        cand.name_legal = Str(c, "name_legal", cand.name_ja);
        cand.kana = Str(c, "kana");
        cand.name_en = Str(c, "name_en", cand.name_ja);
        cand.name_zh = Str(c, "name_zh", cand.name_ja);
        cand.romanization_guess = Bool(c, "romanization_guess");
        cand.party = Str(c, "party", "IND");
        cand.party_ja = Str(c, "party_ja");
        cand.age = Int(c, "age");
        cand.status = Str(c, "status");
        cand.occupation = Str(c, "occupation");
        cand.dual = Bool(c, "dual");
        cand.dual_disqualified = Bool(c, "dual_disqualified");
        cand.sekihairitsu = Num(c, "sekihairitsu");
        cand.incumbent = Bool(c, "incumbent", cand.status == "前");
        cand.pr_elected = Bool(c, "pr_elected");
        cand.photo = Str(c, "photo");
        cand.note = Str(c, "note");
        cand.seats_won = Int(c, "seats_won", -1);
        if (auto it = c.find("list"); it != c.end() && it->is_array()) {
          for (const json& l : *it) {
            ListEntry entry;
            entry.no = Int(l, "no");
            entry.name_ja = Str(l, "name_ja");
            entry.surname_ja = Str(l, "surname_ja");
            entry.given_ja = Str(l, "given_ja");
            entry.smd = Str(l, "smd");
            entry.candidate = Str(l, "candidate");
            entry.order = Int(l, "order");
            entry.disqualified = Bool(l, "disqualified");
            cand.list.push_back(std::move(entry));
          }
        }
        race.candidates.push_back(std::move(cand));
      }
      races_.push_back(std::move(race));
    }
    race_by_id_.clear();
    smd_by_district_.clear();
    pr_by_pref_.clear();
    for (size_t i = 0; i < races_.size(); ++i) {
      const Race& r = races_[i];
      race_by_id_[r.id] = static_cast<int>(i);
      if (r.is_smd()) {
        smd_by_district_[r.region.empty() ? r.id : r.region] = static_cast<int>(i);
      } else {
        for (const std::string& p : r.prefs) pr_by_pref_[p] = static_cast<int>(i);
      }
    }

    info_.dissolution = Str(ej, "dissolution");
    if (auto it = ej.find("election"); it != ej.end() && it->is_object()) {
      if (info_.name_ja.empty()) info_.name_ja = Str(*it, "name_ja");
      if (info_.date.empty()) info_.date = Str(*it, "date");
    }
    info_.offices.clear();
    for (const json& o : ej.value("offices", json::array())) {
      OfficeSummary office;
      office.name_ja = Str(o, "name_ja");
      office.name_zh = Str(o, "name_zh", office.name_ja);
      office.name_en = Str(o, "name_en", office.name_ja);
      office.kind = Str(o, "kind");
      office.seats = Int(o, "seats");
      office.candidates = Int(o, "candidates");
      office.headline = Bool(o, "headline");
      info_.offices.push_back(std::move(office));
    }
    if (auto it = ej.find("totals"); it != ej.end() && it->is_object()) {
      info_.total_seats = Int(*it, "seats");
      info_.majority = Int(*it, "majority", info_.total_seats / 2 + 1);
      info_.supermajority = Int(*it, "supermajority", (info_.total_seats * 2 + 2) / 3);
      info_.total_candidates = Int(*it, "candidates");
    }
    info_.blocs.clear();
    for (const json& b : ej.value("blocs", json::array())) {
      Bloc bloc;
      bloc.id = Str(b, "id");
      bloc.name_ja = Str(b, "name_ja", bloc.id);
      bloc.name_en = Str(b, "name_en", bloc.id);
      bloc.name_zh = Str(b, "name_zh", bloc.name_ja);
      bloc.seats = Int(b, "seats");
      bloc.prefs = Strings(b, "prefs");
      info_.blocs.push_back(std::move(bloc));
    }
    info_.referendums.clear();
    for (const json& r : ej.value("referendums", json::array())) {
      Referendum ref;
      ref.id = Str(r, "id");
      ref.kind = Str(r, "kind", "review");
      ref.justice_ja = Str(r, "justice_ja");
      ref.justice_en = Str(r, "justice_en", ref.justice_ja);
      ref.justice_zh = Str(r, "justice_zh", ref.justice_ja);
      ref.question_ja = Str(r, "question_ja");
      ref.question_en = Str(r, "question_en", ref.question_ja);
      ref.question_zh = Str(r, "question_zh", ref.question_ja);
      ref.bio_ja = Str(r, "bio_ja");
      ref.bio_en = Str(r, "bio_en", ref.bio_ja);
      ref.bio_zh = Str(r, "bio_zh", ref.bio_ja);
      ref.note = Str(r, "note");
      info_.referendums.push_back(std::move(ref));
    }
  } catch (const json::exception& ex) {
    *error = std::string("election JSON schema error: ") + ex.what();
    return false;
  }
  return true;
}

const Party& ElectionData::party(std::string_view code) const {
  for (const Party& p : parties_) {
    if (p.code == code) return p;
  }
  for (const Party& p : parties_) {
    if (p.code == "IND") return p;
  }
  return unknown_party_;
}

const Race* ElectionData::RaceById(std::string_view id) const {
  auto it = race_by_id_.find(std::string(id));
  return it == race_by_id_.end() ? nullptr : &races_[it->second];
}

const Race* ElectionData::SmdRaceForDistrict(std::string_view district_code) const {
  auto it = smd_by_district_.find(std::string(district_code));
  return it == smd_by_district_.end() ? nullptr : &races_[it->second];
}

const Race* ElectionData::PrRaceForPref(std::string_view pref) const {
  auto it = pr_by_pref_.find(std::string(pref));
  return it == pr_by_pref_.end() ? nullptr : &races_[it->second];
}

const Race* ElectionData::PrRaceForBloc(std::string_view bloc_id) const {
  for (const Race& r : races_) {
    if (r.kind == RaceKind::kPr && r.bloc == bloc_id) return &r;
  }
  return nullptr;
}

const Bloc* ElectionData::BlocById(std::string_view bloc_id) const {
  for (const Bloc& b : info_.blocs) {
    if (b.id == bloc_id) return &b;
  }
  return nullptr;
}

const Bloc* ElectionData::BlocForPref(std::string_view pref) const {
  for (const Bloc& b : info_.blocs) {
    for (const std::string& p : b.prefs) {
      if (p == pref) return &b;
    }
  }
  return nullptr;
}

const Candidate* ElectionData::CandidateById(std::string_view id, const Race** race) const {
  for (const Race& r : races_) {
    for (const Candidate& c : r.candidates) {
      if (c.id == id) {
        if (race) *race = &r;
        return &c;
      }
    }
  }
  return nullptr;
}

}  // namespace jpy::election
