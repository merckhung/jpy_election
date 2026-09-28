#include "src/news/feed_parser.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "nlohmann/json.hpp"

namespace jpy::news {
namespace {

void AppendUtf8(std::string* out, unsigned long cp) {
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

// Named entities beyond the XML five that feeds embed in escaped HTML.
const char* NamedEntity(std::string_view ent) {
  static const std::pair<const char*, const char*> kEntities[] = {
      {"nbsp", " "},      {"hellip", "…"},  {"mdash", "—"},  {"ndash", "–"},  {"laquo", "«"},
      {"raquo", "»"},     {"ldquo", "“"},   {"rdquo", "”"},  {"lsquo", "‘"},  {"rsquo", "’"},
      {"middot", "·"},    {"yen", "¥"},     {"copy", "©"},   {"reg", "®"},    {"times", "×"},
      {"bull", "•"},      {"trade", "™"},   {"deg", "°"},    {"ensp", " "},   {"emsp", " "},
      {"thinsp", " "},    {"minus", "−"},
  };
  for (const auto& [name, text] : kEntities) {
    if (ent == name) return text;
  }
  return nullptr;
}

std::string DecodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out.push_back(s[i]);
      continue;
    }
    const size_t semi = s.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) {
      out.push_back('&');
      continue;
    }
    const std::string_view ent = s.substr(i + 1, semi - i - 1);
    if (ent == "amp") out += '&';
    else if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "quot") out += '"';
    else if (ent == "apos") out += '\'';
    else if (const char* named = NamedEntity(ent)) out += named;
    else if (!ent.empty() && ent[0] == '#') {
      const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
      const std::string num(ent.substr(hex ? 2 : 1));
      const unsigned long cp = std::strtoul(num.c_str(), nullptr, hex ? 16 : 10);
      if (cp > 0 && cp <= 0x10FFFF) AppendUtf8(&out, cp);
    } else {
      out.append(s.substr(i, semi - i + 1));
    }
    i = semi;
  }
  return out;
}

bool TagBoundary(char c) {
  return c == '>' || c == ' ' || c == '/' || c == '\t' || c == '\n' || c == '\r';
}

// Returns the inner text of the first <tag ...>...</tag> in [begin, end).
std::string_view Inner(std::string_view xml, std::string_view tag) {
  const std::string open = "<" + std::string(tag);
  size_t p = 0;
  while ((p = xml.find(open, p)) != std::string_view::npos) {
    const char next = p + open.size() < xml.size() ? xml[p + open.size()] : '>';
    if (TagBoundary(next)) break;
    p += open.size();
  }
  if (p == std::string_view::npos) return {};
  const size_t gt = xml.find('>', p);
  if (gt == std::string_view::npos) return {};
  if (xml[gt - 1] == '/') return xml.substr(p, gt - p + 1);  // self-closing: return tag itself
  const std::string close = "</" + std::string(tag) + ">";
  const size_t end = xml.find(close, gt);
  if (end == std::string_view::npos) return {};
  return xml.substr(gt + 1, end - gt - 1);
}

std::string Attr(std::string_view tag_text, std::string_view name) {
  for (const char quote : {'"', '\''}) {
    const std::string key = std::string(name) + "=" + quote;
    size_t p = tag_text.find(key);
    // Whole attribute names only ("href", not "hreflang").
    while (p != std::string_view::npos && p > 0 && !std::isspace(static_cast<unsigned char>(tag_text[p - 1]))) {
      p = tag_text.find(key, p + 1);
    }
    if (p == std::string_view::npos) continue;
    const size_t e = tag_text.find(quote, p + key.size());
    if (e == std::string_view::npos) continue;
    return DecodeEntities(tag_text.substr(p + key.size(), e - p - key.size()));
  }
  return "";
}

std::string LowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Encoding declared in the XML prolog, lower-cased ("" when absent).
std::string DeclaredEncoding(std::string_view xml) {
  if (xml.substr(0, 5) != "<?xml") return "";
  const size_t end = xml.find("?>");
  if (end == std::string_view::npos) return "";
  return LowerAscii(Attr(xml.substr(0, end), "encoding"));
}

}  // namespace

std::string FeedToUtf8(std::string_view xml) {
  if (xml.substr(0, 3) == "\xEF\xBB\xBF") xml.remove_prefix(3);
  const std::string enc = DeclaredEncoding(xml);
#ifdef _WIN32
  UINT cp = 0;
  if (enc == "shift_jis" || enc == "shift-jis" || enc == "sjis" || enc == "windows-31j" ||
      enc == "cp932" || enc == "ms932" || enc == "x-sjis") {
    cp = 932;
  } else if (enc == "euc-jp" || enc == "x-euc-jp") {
    cp = 20932;
  } else if (enc == "iso-2022-jp") {
    cp = 50220;
  }
  if (cp != 0 && !xml.empty()) {
    const int n = static_cast<int>(xml.size());
    const int wn = MultiByteToWideChar(cp, 0, xml.data(), n, nullptr, 0);
    if (wn > 0) {
      std::wstring w(wn, L'\0');
      MultiByteToWideChar(cp, 0, xml.data(), n, w.data(), wn);
      const int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
      std::string out(un, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, out.data(), un, nullptr, nullptr);
      return out;
    }
  }
#endif
  return std::string(xml);
}

std::string CleanText(std::string_view s) {
  std::string text(s);
  // CDATA sections.
  for (size_t p; (p = text.find("<![CDATA[")) != std::string::npos;) {
    const size_t e = text.find("]]>", p);
    if (e == std::string::npos) break;
    text = text.substr(0, p) + text.substr(p + 9, e - p - 9) + text.substr(e + 3);
  }
  text = DecodeEntities(text);  // descriptions are often entity-escaped HTML
  std::string out;
  bool in_tag = false, space = false;
  for (char c : text) {
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '>' && in_tag) {
      in_tag = false;
      space = true;
      continue;
    }
    if (in_tag) continue;
    if (std::isspace(static_cast<unsigned char>(c))) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return DecodeEntities(out);
}

std::vector<store::Article> ParseFeed(std::string_view raw, const std::string& source) {
  std::vector<store::Article> out;
  const std::string text = FeedToUtf8(raw);
  const std::string_view xml = text;
  const bool atom = xml.find("<feed") != std::string_view::npos &&
                    xml.find("<entry") != std::string_view::npos;
  const std::string item_tag = atom ? "entry" : "item";  // RSS 2.0 and RSS 1.0 (RDF)
  const std::string open = "<" + item_tag;
  const std::string close = "</" + item_tag + ">";
  size_t pos = 0;
  while ((pos = xml.find(open, pos)) != std::string_view::npos) {
    const char next = pos + open.size() < xml.size() ? xml[pos + open.size()] : '/';
    if (next == '/' || !TagBoundary(next)) {  // <items>, <item/>
      pos += open.size();
      continue;
    }
    const size_t end = xml.find(close, pos);
    if (end == std::string_view::npos) break;
    const std::string_view item = xml.substr(pos, end - pos);
    pos = end + close.size();
    store::Article a;
    a.source = source;
    a.title = CleanText(Inner(item, "title"));
    if (atom) {
      // <link href="..." rel="alternate"/>: prefer rel="alternate" (or no rel).
      std::string fallback;
      size_t lp = 0;
      while ((lp = item.find("<link", lp)) != std::string_view::npos) {
        const size_t lg = item.find('>', lp);
        if (lg == std::string_view::npos) break;
        const std::string_view tag = item.substr(lp, lg - lp + 1);
        lp = lg;
        if (!TagBoundary(tag.size() > 5 ? tag[5] : '>')) continue;
        const std::string rel = Attr(tag, "rel"), href = Attr(tag, "href");
        if (href.empty()) continue;
        if (rel.empty() || rel == "alternate") {
          a.url = href;
          break;
        }
        if (fallback.empty() && rel != "self" && rel != "enclosure") fallback = href;
      }
      if (a.url.empty()) a.url = fallback;
      a.published_at = CleanText(Inner(item, "published"));
      if (a.published_at.empty()) a.published_at = CleanText(Inner(item, "updated"));
      a.summary = CleanText(Inner(item, "summary"));
      if (a.summary.empty()) a.summary = CleanText(Inner(item, "content"));
    } else {
      a.url = CleanText(Inner(item, "link"));
      if (a.url.empty()) a.url = CleanText(Inner(item, "guid"));
      // RSS 1.0: <item rdf:about="https://...">.
      if (a.url.empty()) a.url = Attr(item.substr(0, item.find('>') + 1), "rdf:about");
      a.published_at = CleanText(Inner(item, "pubDate"));
      if (a.published_at.empty()) a.published_at = CleanText(Inner(item, "dc:date"));
      a.summary = CleanText(Inner(item, "description"));
      if (a.summary.empty()) a.summary = CleanText(Inner(item, "content:encoded"));
      const std::string item_source = CleanText(Inner(item, "source"));
      if (!item_source.empty()) {
        a.source = item_source;  // Google News names the outlet...
        // ...and appends it to the title: "見出し - 朝日新聞".
        const std::string suffix = " - " + item_source;
        if (a.title.size() > suffix.size() &&
            a.title.compare(a.title.size() - suffix.size(), suffix.size(), suffix) == 0) {
          a.title.resize(a.title.size() - suffix.size());
        }
      }
    }
    if (a.summary.size() > 1200) {
      // Cut on a UTF-8 character boundary.
      size_t cut = 1200;
      while (cut > 0 && (static_cast<unsigned char>(a.summary[cut]) & 0xC0) == 0x80) --cut;
      a.summary.resize(cut);
    }
    if (!a.title.empty() && !a.url.empty()) out.push_back(std::move(a));
  }
  return out;
}

std::vector<store::Article> ParseJsonLines(std::string_view text, std::string* error) {
  std::vector<store::Article> out;
  std::istringstream in{std::string(text)};
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
    const nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      if (error) *error = "line " + std::to_string(n) + ": invalid JSON";
      continue;
    }
    store::Article a;
    a.title = j.value("title", "");
    a.url = j.value("url", j.value("link", ""));
    a.source = j.value("source", "");
    a.published_at = j.value("published", j.value("published_at", ""));
    a.summary = j.value("summary", j.value("body", ""));
    if (a.url.empty()) a.url = "jsonl:" + a.source + ":" + a.title;
    if (!a.title.empty()) out.push_back(std::move(a));
  }
  return out;
}

}  // namespace jpy::news
