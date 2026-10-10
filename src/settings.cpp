#include "settings.h"

#include <map>
#include <mutex>

#include "json.h"
#include "util.h"

namespace settings {
namespace {

struct Default {
  const char* key;
  const char* value;
  bool look;
};

const Default kDefaults[] = {

    {"desktop", "on", false},
    {"monitor", "primary", false},
    {"interactive", "on", false},
    {"pauseCovered", "on", false},

    {"position", "center", true},
    {"scale", "100", true},
    {"accent", "album", true},
    {"artShape", "rounded", true},
    {"motion", "on", true},

    {"background", "art", true},
    {"bgColor", "#07080b", true},
    {"bgColor2", "#1d1033", true},
    {"brightness", "normal", true},
    {"blur", "normal", true},
    {"vignette", "on", true},
    {"grain", "on", true},

    {"video", "off", true},
    {"videoMode", "background", true},
    {"videoBrightness", "75", true},
    {"lyrics", "off", true},
    {"lyricsSize", "normal", true},
    {"visualizer", "off", true},
    {"vizIntensity", "normal", true},
    {"vizColor", "accent", true},

    {"clock", "on", true},
    {"clock24", "off", true},
    {"date", "on", true},
    {"art", "on", true},
    {"album", "on", true},
    {"progress", "on", true},
    {"controls", "on", true},
    {"volume", "on", true},
    {"device", "on", true},
    {"idleFade", "on", true},
};

std::mutex g_mutex;
std::map<std::string, std::string> g_values;
std::wstring g_file;

std::string serialize() {
  std::string out = "{";
  bool first = true;
  for (auto& [k, v] : g_values) {
    if (!first) out += ",";
    first = false;
    out += jsonEscape(k) + ":" + jsonEscape(v);
  }
  return out + "}";
}

void save() { writeFile(g_file, serialize() + "\n"); }

}

void init(const std::wstring& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_file = dir + L"\\settings.json";
  std::string text;
  Json saved = readFile(g_file, text) ? Json::parse(text) : Json();

  for (auto& k : saved.keys())
    if (saved[k.c_str()].type() == Json::String) g_values[k] = saved[k.c_str()].str();
  for (auto& d : kDefaults) {
    const Json& v = saved[d.key];
    if (v.type() == Json::String) g_values[d.key] = v.str();
    else if (v.type() == Json::Bool) g_values[d.key] = v.boolean() ? "on" : "off";
    else if (v.type() == Json::Number) g_values[d.key] = std::to_string((int)v.num());
    else g_values[d.key] = d.value;
  }
}

std::string get(const std::string& key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_values.find(key);
  return it == g_values.end() ? std::string() : it->second;
}

bool on(const std::string& key) { return get(key) == "on"; }

void set(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_values[key] = value;
  save();
}

void resetLook() {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& d : kDefaults)
    if (d.look) g_values[d.key] = d.value;
  save();
}

std::string json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return serialize();
}

}
