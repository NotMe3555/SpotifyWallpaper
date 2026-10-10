#include "youtube.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

#include "auth.h"
#include "http_client.h"
#include "json.h"
#include "util.h"

namespace youtube {
namespace {

constexpr ULONGLONG kQuotaBackoffMs = 30 * 60 * 1000, kErrorBackoffMs = 60 * 1000;

std::mutex g_mutex, g_lookupMutex;
std::map<std::string, std::string> g_cache;
std::wstring g_file;
std::string g_lastError;
ULONGLONG g_retryAt;
std::atomic<bool> g_quotaHit;

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


struct Candidate {
  std::string id, title, channel;
};

// Letters and digits only, so "Weird Fishes / Arpeggi" matches "Weird Fishes (Arpeggi)" and "D.C." matches "DC".
std::string squash(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned char c = (unsigned char)text[i];
    if (c == 0xE2 && i + 2 < text.size() && (unsigned char)text[i + 1] == 0x80) i += 2;
    else if (c >= 0x80 || isalnum(c)) out += (char)tolower(c);
  }
  return out;
}

bool isTopicChannel(const std::string& channel) {
  std::string ch = lower(channel);
  return ch.size() > 7 && ch.compare(ch.size() - 7, 7, "- topic") == 0;
}

bool isAscii(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x80; });
}

// Below zero means it isn't this song at all. The site search returns far more results than the API did,
// most of them other songs by the same artist, so the title and artist have to actually be there.
int score(const Candidate& c, const std::string& artist, const std::string& title) {
  std::string t = lower(c.title), ch = lower(c.channel);
  std::string firstArtist = artist.substr(0, artist.find(','));
  std::string wantTitle = squash(cleanTitle(title)), wantArtist = squash(firstArtist);
  std::string gotTitle = squash(c.title), gotChannel = squash(c.channel);
  if (wantTitle.empty() || !has(gotTitle, wantTitle.c_str())) return -1;
  bool byArtist = !wantArtist.empty() && has(gotChannel, wantArtist.c_str());
  bool namesArtist = !wantArtist.empty() && has(gotTitle, wantArtist.c_str());
  if (!byArtist && !namesArtist && isAscii(firstArtist)) return -1;
  if (isTopicChannel(c.channel)) return -1;
  int s = 2 + (byArtist ? 1 : 0);
  if (has(t, "official music video") || has(t, "official video")) s += 4;
  else if (has(t, "music video") || has(t, "(video)") || has(t, "[video]")) s += 2;
  if (has(ch, "vevo")) s += 2;
  for (const char* bad : {"lyric", "audio", "visualizer", "visualiser", "live", "cover", "reaction", "karaoke",
                          "instrumental", "sped up", "slowed", "remix", "8d", "nightcore", "1 hour", "loop",
                          "unofficial", "fan made", "fanmade", "performance", "concert"})
    if (has(t, bad) && !has(lower(cleanTitle(title)), bad)) s -= 3;
  return s;
}

// The search behind youtube.com itself: no key and no daily limit. It's unofficial and can change without
// notice, so a failure here falls back to the Data API when the user has a key.
bool siteSearch(const std::string& query, std::vector<Candidate>& out) {
  std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20240101.00.00\","
                     "\"hl\":\"en\"}},\"query\":" + jsonEscape(query) + "}";
  HttpResponse r = httpRequest(L"POST", L"https://www.youtube.com/youtubei/v1/search?prettyPrint=false",
                               {L"Content-Type: application/json"}, body);
  if (r.status != 200) return false;
  Json d = Json::parse(r.body);
  const Json& sections =
      d["contents"]["twoColumnSearchResultsRenderer"]["primaryContents"]["sectionListRenderer"]["contents"];
  for (size_t i = 0; i < sections.size(); ++i) {
    const Json& items = sections[i]["itemSectionRenderer"]["contents"];
    for (size_t j = 0; j < items.size(); ++j) {
      const Json& v = items[j]["videoRenderer"];
      std::string id = v["videoId"].str();
      if (!id.empty())
        out.push_back({id, v["title"]["runs"][(size_t)0]["text"].str(), v["ownerText"]["runs"][(size_t)0]["text"].str()});
    }
  }
  return sections.size() > 0;
}

bool apiSearch(const std::string& query, const std::string& apiKey, std::vector<Candidate>& out, std::string& error) {
  HttpResponse r = httpRequest(
      L"GET", widen("https://www.googleapis.com/youtube/v3/search?part=snippet&type=video&maxResults=6"
                    "&videoEmbeddable=true&videoSyndicated=true&q=" +
                    urlEncode(query) + "&key=" + urlEncode(apiKey)));
  if (r.status != 200) {
    Json body = Json::parse(r.body);
    error = body["error"]["errors"][(size_t)0]["reason"].str(r.status ? "HTTP " + std::to_string(r.status) : r.error);
    g_quotaHit = r.status == 429 || error == "quotaExceeded" || error == "rateLimitExceeded" ||
                 error == "dailyLimitExceeded";
    return false;
  }
  Json items = Json::parse(r.body)["items"];
  for (size_t i = 0; i < items.size(); ++i)
    out.push_back({items[i]["id"]["videoId"].str(), items[i]["snippet"]["title"].str(),
                   items[i]["snippet"]["channelTitle"].str()});
  g_quotaHit = false;
  return true;
}

// Site results don't say whether a video may be embedded, or that it's one of YouTube's auto-generated
// "Topic" uploads (audio over a still cover). With a key that's one cheap call from the 10,000-a-day pool;
// without one the wallpaper just moves on to the next candidate when a video refuses to play.
void dropUnusable(std::vector<std::string>& ids, const std::string& apiKey) {
  std::string list;
  for (auto& id : ids) list += (list.empty() ? "" : ",") + id;
  HttpResponse r = httpRequest(L"GET", widen("https://www.googleapis.com/youtube/v3/videos?part=status,snippet&id=" +
                                             urlEncode(list) + "&key=" + urlEncode(apiKey)));
  if (r.status != 200) return;
  Json items = Json::parse(r.body)["items"];
  std::vector<std::string> kept;
  for (auto& id : ids)
    for (size_t i = 0; i < items.size(); ++i)
      if (items[i]["id"].str() == id && items[i]["status"]["embeddable"].boolean() &&
          !isTopicChannel(items[i]["snippet"]["channelTitle"].str()))
        kept.push_back(id);
  ids = kept;
}

}

std::string cleanSongTitle(const std::string& title) { return cleanTitle(title); }

void init(const std::wstring& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_file = dir + L"\videos-v3.json";
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
  // Every monitor asks for the same song at once; one lookup serves them all.
  std::lock_guard<std::mutex> lookup(g_lookupMutex);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    if (GetTickCount64() < g_retryAt) {
      error = g_lastError;
      return {};
    }
  }
  std::string apiKey = auth::youtubeKey();
  std::string firstArtist = artist.substr(0, artist.find(','));
  std::string q = firstArtist + " " + cleanTitle(title) + " official music video";
  std::vector<Candidate> found;
  bool fromSite = siteSearch(q, found);
  if (!fromSite) {
    error = "search_unavailable";
    if (apiKey.empty() || !apiSearch(q, apiKey, found, error)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_lastError = error;
      g_retryAt = GetTickCount64() + (g_quotaHit ? kQuotaBackoffMs : kErrorBackoffMs);
      return {};
    }
    error.clear();
  }
  std::vector<std::pair<int, std::string>> ranked;
  for (auto& c : found) {
    int s = score(c, artist, title);
    if (s >= 3) ranked.push_back({s, c.id});
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<std::string> ids;
  for (size_t i = 0; i < ranked.size() && i < 5; ++i) ids.push_back(ranked[i].second);
  if (fromSite && !apiKey.empty() && !ids.empty()) dropUnusable(ids, apiKey);
  std::string best;
  for (size_t i = 0; i < ids.size() && i < 3; ++i) best += (i ? "," : "") + ids[i];
  std::lock_guard<std::mutex> lock(g_mutex);
  g_cache[key] = best;
  save();
  return best;
}

bool quotaExceeded() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_quotaHit && GetTickCount64() < g_retryAt;
}

}
