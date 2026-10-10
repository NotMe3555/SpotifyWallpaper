#include "util.h"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <chrono>

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring exeDir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  std::wstring p(buf, n);
  size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

bool readFile(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (64ll << 20);
  if (ok) {
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
  }
  CloseHandle(h);
  return ok;
}

bool writeFile(const std::wstring& path, const std::string& data) {
  std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
  CloseHandle(h);
  return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void deleteFile(const std::wstring& path) { DeleteFileW(path.c_str()); }

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
    out += kB64[v >> 18 & 63]; out += kB64[v >> 12 & 63]; out += kB64[v >> 6 & 63]; out += kB64[v & 63];
  }
  if (i < in.size()) {
    uint32_t v = (uint8_t)in[i] << 16 | (i + 1 < in.size() ? (uint8_t)in[i + 1] << 8 : 0);
    out += kB64[v >> 18 & 63]; out += kB64[v >> 12 & 63];
    out += i + 1 < in.size() ? kB64[v >> 6 & 63] : '=';
    out += '=';
  }
  return out;
}

std::string base64Decode(const std::string& in) {
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (char c : in) {
    const char* p = c ? strchr(kB64, c) : nullptr;
    if (!p) continue;
    buf = buf << 6 | (uint32_t)(p - kB64);
    bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)(buf >> bits & 0xFF); }
  }
  return out;
}

std::string urlEncode(const std::string& s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else out += s[i];
  }
  return out;
}

std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> out;
  size_t start = 0;
  while (start <= q.size()) {
    size_t amp = q.find('&', start);
    if (amp == std::string::npos) amp = q.size();
    std::string part = q.substr(start, amp - start);
    if (!part.empty()) {
      size_t eq = part.find('=');
      if (eq == std::string::npos) out[urlDecode(part)] = "";
      else out[urlDecode(part.substr(0, eq))] = urlDecode(part.substr(eq + 1));
    }
    start = amp + 1;
  }
  return out;
}

std::string randomHex(size_t bytes) {
  std::vector<unsigned char> buf(bytes);
  BCryptGenRandom(nullptr, buf.data(), (ULONG)bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  static const char hex[] = "0123456789abcdef";
  std::string out;
  for (unsigned char b : buf) { out += hex[b >> 4]; out += hex[b & 15]; }
  return out;
}

int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string dpapiProtect(const std::string& plain) {
  DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()}, out{};
  if (!CryptProtectData(&in, L"SpotifyWallpaper", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    return {};
  std::string blob((char*)out.pbData, out.cbData);
  LocalFree(out.pbData);
  return blob;
}

bool dpapiUnprotect(const std::string& blob, std::string& plain) {
  DATA_BLOB in{(DWORD)blob.size(), (BYTE*)blob.data()}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
  plain.assign((char*)out.pbData, out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return true;
}

std::string formatTime(int64_t ms) {
  int64_t s = ms < 0 ? 0 : ms / 1000;
  char buf[32];
  snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
  return buf;
}
