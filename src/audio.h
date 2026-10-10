

#pragma once
#include <windows.h>

#include <vector>

namespace audio {

constexpr int kBands = 48;


void start(HWND hwnd, UINT msg);
void stop();
bool running();

}
