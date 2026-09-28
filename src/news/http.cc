#include "src/news/http.h"

#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#else
#include "curl/curl.h"
#endif

namespace jpy::news {
namespace {

#ifdef _WIN32

constexpr wchar_t kUserAgent[] = L"jpy_election/0.2 (+news monitor)";

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string LastError(const char* what) {
  const DWORD code = GetLastError();
  char buf[64];
  std::snprintf(buf, sizeof buf, " (WinHTTP error %lu)", static_cast<unsigned long>(code));
  switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return std::string(what) + ": timed out" + buf;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return std::string(what) + ": could not resolve host" + buf;
    case ERROR_WINHTTP_CANNOT_CONNECT: return std::string(what) + ": could not connect" + buf;
    case ERROR_WINHTTP_SECURE_FAILURE: return std::string(what) + ": TLS failure" + buf;
    default: return std::string(what) + buf;
  }
}

// file:// URLs (tests, feeds dropped on disk). Mirrors libcurl: status 0.
bool ReadFileUrl(const std::string& url, HttpResponse* r) {
  constexpr char kScheme[] = "file://";
  if (url.compare(0, sizeof kScheme - 1, kScheme) != 0) return false;
  std::string path = url.substr(sizeof kScheme - 1);
  if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1);  // /C:/...
  std::ifstream f(std::filesystem::path(Widen(path)), std::ios::binary);
  if (!f) {
    r->error = "Couldn't read a file:// file";
    return true;
  }
  r->body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET v) : h(v) {}
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  explicit operator bool() const { return h != nullptr; }
};

HttpResponse Perform(const std::string& url, const std::string* post_body,
                     const std::vector<std::string>& headers, long timeout_s) {
  HttpResponse r;
  if (ReadFileUrl(url, &r)) return r;

  const std::wstring wurl = Widen(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof uc;
  uc.dwSchemeLength = static_cast<DWORD>(-1);
  uc.dwHostNameLength = static_cast<DWORD>(-1);
  uc.dwUrlPathLength = static_cast<DWORD>(-1);
  uc.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
    r.error = LastError("URL using bad/illegal format");
    return r;
  }
  const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
  std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  if (path.empty()) path = L"/";
  const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

  Handle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    // Pre-Windows 8.1 fallback.
    session.h = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session) {
    r.error = LastError("WinHttpOpen");
    return r;
  }
  const int timeout_ms = static_cast<int>(std::max<long>(1, timeout_s) * 1000);
  WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                        sizeof protocols)) {
    protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
  }
  // Transparent gzip/deflate decoding (Windows 8.1+; ignored if unsupported).
  DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
  WinHttpSetOption(session.h, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                   sizeof decompression);

  Handle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
  if (!connect) {
    r.error = LastError("WinHttpConnect");
    return r;
  }
  Handle request(WinHttpOpenRequest(connect.h, post_body ? L"POST" : L"GET", path.c_str(),
                                    nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    https ? WINHTTP_FLAG_SECURE : 0));
  if (!request) {
    r.error = LastError("WinHttpOpenRequest");
    return r;
  }
  // Follow redirects (like CURLOPT_FOLLOWLOCATION), including https -> http.
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);

  std::wstring all_headers;
  for (const std::string& h : headers) all_headers += Widen(h) + L"\r\n";
  if (post_body) all_headers += L"Content-Type: application/json\r\n";
  if (!all_headers.empty()) {
    WinHttpAddRequestHeaders(request.h, all_headers.c_str(), static_cast<DWORD>(all_headers.size()),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  const DWORD body_size = post_body ? static_cast<DWORD>(post_body->size()) : 0;
  void* body = post_body ? const_cast<char*>(post_body->data()) : WINHTTP_NO_REQUEST_DATA;
  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, body_size, body_size,
                          0)) {
    r.error = LastError("WinHttpSendRequest");
    return r;
  }
  if (!WinHttpReceiveResponse(request.h, nullptr)) {
    r.error = LastError("WinHttpReceiveResponse");
    return r;
  }
  DWORD status = 0, status_size = sizeof status;
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(request.h, &avail)) {
      r.error = LastError("WinHttpQueryDataAvailable");
      return r;
    }
    if (avail == 0) break;
    const size_t old = r.body.size();
    r.body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, r.body.data() + old, avail, &read)) {
      r.body.resize(old);
      r.error = LastError("WinHttpReadData");
      return r;
    }
    r.body.resize(old + read);
  }
  r.status = static_cast<long>(status);
  return r;
}

#else  // !_WIN32

size_t Append(char* data, size_t size, size_t n, void* user) {
  static_cast<std::string*>(user)->append(data, size * n);
  return size * n;
}

void GlobalInit() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse Perform(const std::string& url, const std::string* post_body,
                     const std::vector<std::string>& headers, long timeout_s) {
  GlobalInit();
  HttpResponse r;
  CURL* curl = curl_easy_init();
  if (!curl) {
    r.error = "curl_easy_init failed";
    return r;
  }
  curl_slist* list = nullptr;
  for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());
  if (post_body) list = curl_slist_append(list, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "jpy_election/0.2 (+news monitor)");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // any supported compression
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Append);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  if (post_body) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
  }
  const CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    r.error = curl_easy_strerror(rc);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
  }
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);
  return r;
}

#endif  // _WIN32

}  // namespace

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers,
                     long timeout_s) {
  return Perform(url, nullptr, headers, timeout_s);
}

HttpResponse HttpPostJson(const std::string& url, const std::string& json_body,
                          const std::vector<std::string>& headers, long timeout_s) {
  return Perform(url, &json_body, headers, timeout_s);
}

}  // namespace jpy::news
