#include "http_client.h"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

static HINTERNET session() {
  static HINTERNET s = [] {
    HINTERNET h = WinHttpOpen(L"SpotifyWallpaper/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (h) {
      WinHttpSetTimeouts(h, 5000, 8000, 10000, 15000);
      DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
      WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    return h;
  }();
  return s;
}

HttpResponse httpRequest(const std::wstring& method, const std::wstring& url,
                         const std::vector<std::wstring>& headers, const std::string& body) {
  HttpResponse res;
  URL_COMPONENTS uc{sizeof uc};
  wchar_t host[256], path[4096];
  uc.lpszHostName = host; uc.dwHostNameLength = (DWORD)std::size(host);
  uc.lpszUrlPath = path; uc.dwUrlPathLength = (DWORD)std::size(path);
  uc.dwExtraInfoLength = 1;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { res.error = "Bad URL"; return res; }
  std::wstring fullPath(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.lpszExtraInfo) fullPath.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

  HINTERNET conn = WinHttpConnect(session(), host, uc.nPort, 0);
  if (!conn) { res.error = "Couldn't connect"; return res; }
  DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(conn, method.c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) { WinHttpCloseHandle(conn); res.error = "Couldn't open request"; return res; }

  std::wstring hdrs;
  for (auto& h : headers) hdrs += h + L"\r\n";
  BOOL ok = WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), (DWORD)-1L,
                               body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                               (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr);
  if (ok) {
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    res.status = (int)status;
    wchar_t retry[32] = {};
    len = sizeof retry;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &len, WINHTTP_NO_HEADER_INDEX))
      res.retryAfterSec = _wtoi(retry);
    for (;;) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
      size_t old = res.body.size();
      res.body.resize(old + avail);
      DWORD read = 0;
      if (!WinHttpReadData(req, res.body.data() + old, avail, &read)) { res.body.resize(old); break; }
      res.body.resize(old + read);
      if (res.body.size() > (32u << 20)) break;
    }
  } else {
    res.error = "Network error (" + std::to_string(GetLastError()) + ")";
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(conn);
  return res;
}
