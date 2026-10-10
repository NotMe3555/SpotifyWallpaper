

#pragma once
#include <string>

namespace auth {

constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 8888;

void init(const std::wstring& dir);

std::string youtubeKey();
bool saveYoutubeKey(const std::string& key);

}
