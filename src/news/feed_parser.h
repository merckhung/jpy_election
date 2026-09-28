// RSS 2.0 / RSS 1.0 (RDF) / Atom feed parsing into articles (title, link,
// date, summary). A deliberately small, tolerant tag scanner: news feeds are
// simple and often not well-formed enough for a strict XML parser.
//
// Japanese outlets (NHK, 朝日, 毎日, 読売, 日経, 共同, 時事, Yahoo!ニュース,
// Google ニュース) serve UTF-8; a UTF-8 BOM is skipped, and on Windows feeds
// declaring Shift_JIS / EUC-JP / ISO-2022-JP are converted to UTF-8.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "src/store/db.h"

namespace jpy::news {

std::vector<store::Article> ParseFeed(std::string_view xml, const std::string& source);

// Parses one article per line: {"title", "url", "source", "published", "summary"}.
std::vector<store::Article> ParseJsonLines(std::string_view text, std::string* error);

// Decodes entities/CDATA and strips HTML tags; collapses whitespace.
std::string CleanText(std::string_view s);

// The feed as UTF-8: strips a BOM and converts legacy Japanese encodings
// declared in the XML prolog (Windows only; returned unchanged elsewhere).
std::string FeedToUtf8(std::string_view xml);

}  // namespace jpy::news
