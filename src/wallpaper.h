

#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace wallpaper {

struct Options {
  std::string monitor = "primary";
  bool interactive = true;
  bool pauseCovered = true;
};

struct Monitor {
  std::string id;
  std::wstring label;
  bool primary = false;
};
std::vector<Monitor> monitors();


bool start(HINSTANCE inst, const std::wstring& url, const Options& options, std::wstring& error);
void stop();
bool running();
void setOptions(const Options& options);
void restart();
void reposition();
void runScript(const std::wstring& js);
void postJson(const std::wstring& json);
bool anyVisible();

}
