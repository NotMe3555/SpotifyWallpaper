#include "auth.h"

#include <map>
#include <mutex>

#include "http_client.h"
#include "json.h"
#include "util.h"

namespace auth {
namespace {

struct Tokens {
  std::string access, refresh;
  int64_t expiresAt = 0;
};

std::mutex g_mutex;
std::mutex g_refreshMutex;
std::wstring g_configFile, g_tokenFile;
Credentials g_creds;

const char* const kSecretNames[] = {"youtubeKey"};
std::map<std::string, std::string> g_secrets;
Tokens g_tokens;
std::map<std::string, int64_t> g_pendingStates;
HWND g_notifyHwnd = nullptr;
UINT g_notifyMsg = 0;

bool isPlaceholder(const std::string& s) { return s.empty() || s.rfind("PASTE_", 0) == 0; }

std::string envVar(const char* name) {
  char buf[512];
  DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
  return n && n < sizeof buf ? std::string(buf, n) : std::string();
}

void notify() {
  if (g_notifyHwnd) PostMessageW(g_notifyHwnd, g_notifyMsg, 0, 0);
}

void loadConfig() {
  std::string text;
  Json cfg = readFile(g_configFile, text) ? Json::parse(text) : Json();
  g_creds.clientId = cfg["clientId"].str();
  g_creds.clientSecret.clear();
  std::string protectedSecret = cfg["clientSecretProtected"].str();
  if (!protectedSecret.empty()) dpapiUnprotect(base64Decode(protectedSecret), g_creds.clientSecret);
  if (g_creds.clientSecret.empty()) g_creds.clientSecret = cfg["clientSecret"].str();
  g_secrets.clear();
  for (const char* name : kSecretNames) {
    std::string value, protectedValue = cfg[(std::string(name) + "Protected").c_str()].str();
    if (!protectedValue.empty()) dpapiUnprotect(base64Decode(protectedValue), value);
    if (value.empty()) value = cfg[name].str();
    if (!value.empty()) g_secrets[name] = value;
  }
  if (isPlaceholder(g_creds.clientId)) g_creds.clientId.clear();
  if (isPlaceholder(g_creds.clientSecret)) g_creds.clientSecret.clear();

  std::string envId = envVar("SPOTIFY_CLIENT_ID"), envSecret = envVar("SPOTIFY_CLIENT_SECRET");
  if (!envId.empty() && !envSecret.empty()) {
    g_creds.clientId = envId;
    g_creds.clientSecret = envSecret;
    g_creds.fromEnvironment = true;
  }
}


bool writeConfig(const std::string& id, const std::string& secret,
                 const std::map<std::string, std::string>& secrets) {
  std::string out = "{\n  \"clientId\": " + jsonEscape(id);
  if (!secret.empty())
    out += ",\n  \"clientSecretProtected\": " + jsonEscape(base64Encode(dpapiProtect(secret)));
  for (auto& [name, value] : secrets)
    if (!value.empty())
      out += ",\n  " + jsonEscape(name + "Protected") + ": " + jsonEscape(base64Encode(dpapiProtect(value)));
  out += "\n}\n";
  return writeFile(g_configFile, out);
}

void loadTokens() {
  std::string text;
  if (!readFile(g_tokenFile, text)) return;
  Json t = Json::parse(text);
  g_tokens.access = t["access_token"].str();
  g_tokens.refresh = t["refresh_token"].str();
  g_tokens.expiresAt = (int64_t)t["expires_at"].num();
}


void storeTokens(const Tokens& t) {
  g_tokens = t;
  if (t.refresh.empty()) {
    deleteFile(g_tokenFile);
    return;
  }
  writeFile(g_tokenFile, "{\n  \"access_token\": " + jsonEscape(t.access) +
                             ",\n  \"refresh_token\": " + jsonEscape(t.refresh) +
                             ",\n  \"expires_at\": " + std::to_string(t.expiresAt) + "\n}\n");
}


Json tokenRequest(const Credentials& c, const std::string& form, int& status, std::string& error) {
  std::string basic = base64Encode(c.clientId + ":" + c.clientSecret);
  HttpResponse res = httpRequest(L"POST", L"https://accounts.spotify.com/api/token",
                                 {L"Content-Type: application/x-www-form-urlencoded",
                                  L"Authorization: Basic " + widen(basic)},
                                 form);
  status = res.status;
  Json body = Json::parse(res.body);
  if (res.status != 200) {
    error = body["error_description"].str(body["error"].str(res.error.empty() ? "HTTP " + std::to_string(res.status)
                                                                            : res.error));
    if (body["error"].str() == "invalid_client") error = "Spotify rejected the Client ID or Client Secret";
  }
  return body;
}

}

void init(const std::wstring& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_configFile = dir + L"\\config.json";
  g_tokenFile = dir + L"\\tokens.json";
  loadConfig();
  loadTokens();
}

void setNotifyWindow(HWND hwnd, UINT msg) {
  g_notifyHwnd = hwnd;
  g_notifyMsg = msg;
}

Credentials credentials() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_creds;
}

bool hasCredentials() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return !g_creds.clientId.empty() && !g_creds.clientSecret.empty();
}

bool saveCredentials(const std::string& id, const std::string& secret) {
  bool changed;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    changed = id != g_creds.clientId;
    if (!writeConfig(id, secret, g_secrets)) return false;
    g_creds.clientId = id;
    g_creds.clientSecret = secret;
    g_creds.fromEnvironment = false;

    if (changed && !g_tokens.refresh.empty()) storeTokens({});
  }
  if (changed) notify();
  return true;
}

std::string secret(const char* name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_secrets.find(name);
  return it == g_secrets.end() ? std::string() : it->second;
}

bool saveSecret(const char* name, const std::string& value) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto next = g_secrets;
  next[name] = value;
  bool env = g_creds.fromEnvironment;
  if (!writeConfig(env ? std::string() : g_creds.clientId, env ? std::string() : g_creds.clientSecret, next))
    return false;
  g_secrets = next;
  return true;
}

std::string youtubeKey() { return secret("youtubeKey"); }
bool saveYoutubeKey(const std::string& key) { return saveSecret("youtubeKey", key); }

bool isConnected() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return !g_tokens.refresh.empty();
}

std::string accessToken(int64_t* expiresAt) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_tokens.refresh.empty()) return {};
    if (nowMs() < g_tokens.expiresAt - 60000) {
      if (expiresAt) *expiresAt = g_tokens.expiresAt;
      return g_tokens.access;
    }
  }

  std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
  Tokens current;
  Credentials creds;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    current = g_tokens;
    creds = g_creds;
  }
  if (current.refresh.empty()) return {};
  if (nowMs() < current.expiresAt - 60000) {
    if (expiresAt) *expiresAt = current.expiresAt;
    return current.access;
  }

  int status = 0;
  std::string error;
  Json t = tokenRequest(creds, "grant_type=refresh_token&refresh_token=" + urlEncode(current.refresh), status, error);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (status != 200) {
    OutputDebugStringA(("Token refresh failed: " + error + "\n").c_str());
    if (status == 400 || status == 401) {
      storeTokens({});
      notify();
    }
    return {};
  }
  Tokens fresh;
  fresh.access = t["access_token"].str();
  fresh.refresh = t["refresh_token"].str(current.refresh);
  fresh.expiresAt = nowMs() + (int64_t)t["expires_in"].num(3600) * 1000;
  storeTokens(fresh);
  if (expiresAt) *expiresAt = fresh.expiresAt;
  return fresh.access;
}

std::string beginLogin() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string state = randomHex(16);
  int64_t now = nowMs();
  for (auto it = g_pendingStates.begin(); it != g_pendingStates.end();)
    it = now - it->second > 15 * 60000 ? g_pendingStates.erase(it) : std::next(it);
  g_pendingStates[state] = now;
  return "https://accounts.spotify.com/authorize?client_id=" + urlEncode(g_creds.clientId) +
         "&response_type=code&redirect_uri=" + urlEncode(kRedirectUri) + "&scope=" + urlEncode(kScopes) +
         "&state=" + state + "&show_dialog=false";
}

bool completeLogin(const std::string& state, const std::string& code, std::string& error) {
  Credentials creds;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pendingStates.erase(state)) {
      error = "This login link expired. Please try again.";
      return false;
    }
    creds = g_creds;
  }
  int status = 0;
  Json t = tokenRequest(creds,
                        "grant_type=authorization_code&code=" + urlEncode(code) +
                            "&redirect_uri=" + urlEncode(kRedirectUri),
                        status, error);
  if (status != 200) return false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    storeTokens({t["access_token"].str(), t["refresh_token"].str(),
                 nowMs() + (int64_t)t["expires_in"].num(3600) * 1000});
  }
  notify();
  return true;
}

void logout() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    storeTokens({});
  }
  notify();
}

}
