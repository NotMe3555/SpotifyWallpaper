




#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace spotifycap {

constexpr size_t kChunkFrames = 2880;

using Sink = std::function<void(const int16_t* stereo, size_t frames)>;

void recoverMixer();
void start(Sink sink);
void stop();
bool running();
void setMuteLocal(bool mute);
float currentGain();
std::string takeStats();

}
