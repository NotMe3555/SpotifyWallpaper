#include "youtube.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>

#include "auth.h"
#include "http_client.h"
#include "json.h"
#include "util.h"

namespace youtube {
namespace {

std::mutex g_mutex;
std::map<std::string, std::string> g_cache;
std::wstring g_file;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
  return s;
}

bool has(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }



std::string cleanTitle(const std::string& title) {
  static const char* const noise[] = {"feat", "ft.", "with ", "remaster", "version", "edit", "from ", "soundtrack",
                                      "mono", "stereo", "single", "anniversary", "deluxe", "bonus", "live"};
  auto isNoise = [&](const std::string& part) {
    std::string l = lower(part);
    for (const char* n : noise)
      if (has(l, n)) return true;
    return false;
  };
  std::string t = title;
  for (size_t open; (open = t.find_first_of("([")) != std::string::npos;) {
    size_t close = t.find_first_of(")]", open);
    if (close == std::string::npos || !isNoise(t.substr(open, close - open))) break;
    t.erase(open, close - open + 1);
  }
  size_t dash = t.find(" - ");
  if (dash != std::string::npos && isNoise(t.substr(dash))) t.erase(dash);
  while (!t.empty() && t.back() == ' ') t.pop_back();
  return t.empty() ? title : t;
}

void save() {
  std::string out = "{";
  bool first = true;
  for (auto& [k, v] : g_cache) {
    out += (first ? "\n  " : ",\n  ") + jsonEscape(k) + ": " + jsonEscape(v);
    first = false;
  }
  writeFile(g_file, out + "\n}\n");
}


int score(const Json& item, const std::string& artist, const std::string& title) {
  std::string t = lower(item["snippet"]["title"].str()), ch = lower(item["snippet"]["channelTitle"].str());
  std::string a = lower(artist);
  std::string firstArtist = a.substr(0, a.find(','));
  int s = 0;
  if (has(t, "official music video") || has(t, "official video")) s += 4;
  else if (has(t, "music video") || has(t, "(video)") || has(t, "[video]")) s += 2;
  if (has(ch, "vevo")) s += 2;
  if (!firstArtist.empty() && (has(ch, firstArtist.c_str()) || has(t, firstArtist.c_str()))) s += 1;
  if (has(t, lower(cleanTitle(title)).substr(0, 24).c_str())) s += 1;
  for (const char* bad : {"lyric", "audio", "visualizer", "visualiser", "live", "cover", "reaction", "karaoke",
                          "instrumental", "sped up", "slowed", "remix", "8d", "nightcore", "1 hour", "loop"})
    if (has(t, bad) && !has(lower(cleanTitle(title)), bad)) s -= 3;
  return s;
}

}

std::string cleanSongTitle(const std::string& title) { return cleanTitle(title); }

void init(const std::wstring& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_file = dir + L"\\videos-v3.json";
  std::string text;
  if (!readFile(g_file, text)) return;
  Json j = Json::parse(text);
  for (auto& k : j.keys()) g_cache[k] = j[k.c_str()].str();
}

std::string findVideo(const std::string& artist, const std::string& title, std::string& error) {
  std::string key = lower(artist) + "|" + lower(title);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
  }
  std::string apiKey = auth::youtubeKey();
  if (apiKey.empty()) {
    error = "no_key";
    return {};
  }
  std::string firstArtist = artist.substr(0, artist.find(','));
  std::string q = firstArtist + " " + cleanTitle(title) + " official music video";
  HttpResponse r = httpRequest(
      L"GET", widen("https://www.googleapis.com/youtube/v3/search?part=snippet&type=video&maxResults=6"
                    "&videoEmbeddable=true&videoSyndicated=true&q=" +
                    urlEncode(q) + "&key=" + urlEncode(apiKey)));
  if (r.status != 200) {
    Json body = Json::parse(r.body);
    error = body["error"]["errors"][(size_t)0]["reason"].str(r.status ? "HTTP " + std::to_string(r.status) : r.error);
    return {};
  }
  Json items = Json::parse(r.body)["items"];
  std::vector<std::pair<int, std::string>> ranked;
  for (size_t i = 0; i < items.size(); ++i) {
    int s = score(items[i], artist, title);
    std::string id = items[i]["id"]["videoId"].str();
    if (s >= 2 && !id.empty()) ranked.push_back({s, id});
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });
  std::string best;
  for (size_t i = 0; i < ranked.size() && i < 3; ++i) best += (i ? "," : "") + ranked[i].second;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_cache[key] = best;
  save();
  return best;
}

}
