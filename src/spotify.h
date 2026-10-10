
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spotify {

struct Player {
  bool hasItem = false;
  bool playing = false;
  std::string id, title, artist, album, artUrl;
  int64_t progressMs = 0, durationMs = 0;
};

struct Art {
  std::string url;
  std::vector<uint8_t> pixels;
  uint32_t width = 0, height = 0;
  float accent[3] = {0.67f, 0.73f, 0.82f};
};

Player fetchPlayer();
std::string playerJson();
bool decodeArt(const std::string& url, Art& out);

enum class Command { Play, Pause, Next, Previous };

std::string send(Command cmd);

}
