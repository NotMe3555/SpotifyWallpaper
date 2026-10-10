
#pragma once
#include <cstdint>
#include <string>

namespace media {

struct State {
  bool open = false;
  bool playing = false;
  std::string title, artist, album;
  int64_t positionMs = 0, durationMs = 0;
  uint32_t artVersion = 0;
};

void start(void (*onChange)());

State state();
std::string art();

enum class Command { Play, Pause, Next, Previous };
bool send(Command cmd);
bool seek(int64_t positionMs);

int volume();
bool setVolume(int percent);

}
