#include "lyrics.h"

#include <windows.h>

#include <cmath>
#include <map>
#include <mutex>

#include "http_client.h"
#include "json.h"
#include "util.h"
#include "youtube.h"

namespace lyrics {
namespace {

std::mutex g_mutex;
std::map<std::string, std::string> g_cache;

const std::vector<std::wstring> kHeaders = {L"User-Agent: SpotifyWallpaper/1.0 (desktop wallpaper)"};

std::string result(const Json& j) {
  return "{\"synced\":" + jsonEscape(j["syncedLyrics"].str()) + ",\"plain\":" + jsonEscape(j["plainLyrics"].str()) +
         ",\"instrumental\":" + (j["instrumental"].boolean() ? "true" : "false") + ",\"error\":\"\"}";
}

}

std::string find(const std::string& artist, const std::string& title, const std::string& album, int durationSec) {
  std::string key = artist + "|" + title + "|" + std::to_string(durationSec);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
  }
  std::string firstArtist = artist.substr(0, artist.find(','));
  std::string out;


  HttpResponse r = httpRequest(
      L"GET", widen("https://lrclib.net/api/get?artist_name=" + urlEncode(firstArtist) +
                    "&track_name=" + urlEncode(title) + "&album_name=" + urlEncode(album) +
                    "&duration=" + std::to_string(durationSec)),
      kHeaders);
  if (r.status == 200) {
    Json j = Json::parse(r.body);
    if (!j["syncedLyrics"].str().empty() || j["instrumental"].boolean()) out = result(j);
  }


  if (out.empty()) {
    HttpResponse s = httpRequest(L"GET",
                                 widen("https://lrclib.net/api/search?artist_name=" + urlEncode(firstArtist) +
                                       "&track_name=" + urlEncode(youtube::cleanSongTitle(title))),
                                 kHeaders);
    if (s.status == 200) {
      Json list = Json::parse(s.body);
      int best = -1, bestScore = -1;
      for (size_t i = 0; i < list.size(); ++i) {
        const Json& item = list[i];
        int score = 0;
        if (!item["syncedLyrics"].str().empty()) score += 4;
        else if (!item["plainLyrics"].str().empty()) score += 1;
        else continue;
        double diff = std::fabs(item["duration"].num() - durationSec);
        if (durationSec > 0 && diff <= 3) score += 3;
        else if (durationSec > 0 && diff <= 10) score += 1;
        else if (durationSec > 0 && diff > 30) score -= 3;
        if (score > bestScore) bestScore = score, best = (int)i;
      }
      if (best >= 0 && bestScore > 0) out = result(list[(size_t)best]);
    } else if (s.status == 0 || s.status >= 500) {

      return "{\"synced\":\"\",\"plain\":\"\",\"instrumental\":false,\"error\":" +
             jsonEscape(s.status ? "HTTP " + std::to_string(s.status) : s.error) + "}";
    }
  }
  if (out.empty()) out = "{\"synced\":\"\",\"plain\":\"\",\"instrumental\":false,\"error\":\"\"}";

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_cache.size() > 500) g_cache.clear();
  g_cache[key] = out;
  return out;
}

}
