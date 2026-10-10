
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spotify {

struct Player {
  bool ok = false;
  bool hasItem = false;
  bool playing = false;
  std::string id, title, artist, album, artUrl, device;
  int64_t progressMs = 0, durationMs = 0;
  int status = 0;
  int retryAfterSec = 0;
};

struct Art {
  std::string url;
  std::vector<uint8_t> pixels;
  uint32_t width = 0, height = 0;
  float accent[3] = {0.67f, 0.73f, 0.82f};
};

Player fetchPlayer();
std::string fetchDisplayName();
bool decodeArt(const std::string& url, Art& out);

enum class Command { Play, Pause, Next, Previous };

std::string send(Command cmd);

}
