

#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace auth {

constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 8888;
constexpr const char* kRedirectUri = "http://127.0.0.1:8888/callback";
constexpr const char* kScopes = "user-read-playback-state user-modify-playback-state user-read-currently-playing";

struct Credentials {
  std::string clientId;
  std::string clientSecret;
  bool fromEnvironment = false;
};

void init(const std::wstring& dir);
void setNotifyWindow(HWND hwnd, UINT msg);

Credentials credentials();
bool hasCredentials();
bool saveCredentials(const std::string& id, const std::string& secret);


std::string youtubeKey();
bool saveYoutubeKey(const std::string& key);

bool isConnected();

std::string accessToken(int64_t* expiresAt = nullptr);

std::string beginLogin();
bool completeLogin(const std::string& state, const std::string& code, std::string& error);
void logout();

}
