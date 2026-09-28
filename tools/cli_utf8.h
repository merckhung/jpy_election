// Command-line helpers shared by the C++ tools: on Windows, switches the
// console to UTF-8 output and re-reads the arguments as UTF-8 (argv is in the
// ANSI code page there, which mangles Japanese text such as --text="…").
#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace jpy::tools {

inline std::vector<std::string> Utf8Args(int argc, char** argv) {
  std::vector<std::string> out;
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  int n = 0;
  if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
    for (int i = 0; i < n; ++i) {
      const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
      std::string s(len > 0 ? len - 1 : 0, '\0');
      if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
      out.push_back(std::move(s));
    }
    LocalFree(w);
    if (n == argc) return out;
    out.clear();
  }
#endif
  for (int i = 0; i < argc; ++i) out.push_back(argv[i]);
  return out;
}

}  // namespace jpy::tools
