// Makes the relative paths the tests use ("data/...", "assets/...") resolve
// on every platform. On Windows Bazel does not build a runfiles tree by
// default, only a MANIFEST that maps runfile paths to source files; in that
// case we change into the source directory the data files come from.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace jpy::testing {

inline bool EnterDataRoot() {
  std::error_code ec;
  if (std::filesystem::exists("data/election/2026/parties.json", ec)) return true;
  const char* manifest = std::getenv("RUNFILES_MANIFEST_FILE");
  if (!manifest) return false;
  std::ifstream in(manifest);
  std::string line;
  const std::string key = "_main/data/election/2026/parties.json ";
  while (std::getline(in, line)) {
    if (line.rfind(key, 0) != 0) continue;
    std::string target = line.substr(key.size());
    while (!target.empty() && (target.back() == '\r' || target.back() == ' ')) target.pop_back();
    // <root>/data/election/2026/parties.json -> <root>
    const std::filesystem::path root =
        std::filesystem::path(target).parent_path().parent_path().parent_path().parent_path();
    std::filesystem::current_path(root, ec);
    return !ec;
  }
  return false;
}

// Runs before main() in every test binary that includes this header.
inline const bool kDataRootEntered = EnterDataRoot();

}  // namespace jpy::testing
