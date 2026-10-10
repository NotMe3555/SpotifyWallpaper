

#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace discordbot {

enum class State { Off, Connecting, Online, Error };

struct Status {
  State state = State::Off;
  std::string botName;
  std::string error;
  std::string guild, channel;
  uint64_t appId = 0;
  bool streaming = false;
};

enum class Output { Speakers, Discord, Both };

void start(const std::string& token, HWND notify, UINT msg);
void stop();
Status status();
void setOutput(Output output);
void sync();
void setPresenceEnabled(bool on);
void setNowPlaying(const std::string& title, const std::string& artist, const std::string& album,
                   const std::string& artUrl, bool playing);
void leaveVoice();
std::string inviteUrl();

}
