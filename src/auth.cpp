#include "auth.h"

#include <mutex>

#include "json.h"
#include "util.h"

namespace auth {
namespace {

std::mutex g_mutex;
std::wstring g_configFile;
std::string g_youtubeKey;

}

void init(const std::wstring& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_configFile = dir + L"\\config.json";
  std::string text;
  Json cfg = readFile(g_configFile, text) ? Json::parse(text) : Json();
  std::string protectedKey = cfg["youtubeKeyProtected"].str();
  if (!protectedKey.empty()) dpapiUnprotect(base64Decode(protectedKey), g_youtubeKey);
  if (g_youtubeKey.empty()) g_youtubeKey = cfg["youtubeKey"].str();
}

std::string youtubeKey() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_youtubeKey;
}

bool saveYoutubeKey(const std::string& key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string out = "{";
  if (!key.empty()) out += "\n  \"youtubeKeyProtected\": " + jsonEscape(base64Encode(dpapiProtect(key)));
  if (!writeFile(g_configFile, out + "\n}\n")) return false;
  g_youtubeKey = key;
  return true;
}

}
