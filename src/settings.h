

#pragma once
#include <string>

namespace settings {

void init(const std::wstring& dir);
std::string get(const std::string& key);
bool on(const std::string& key);
void set(const std::string& key, const std::string& value);
void resetLook();
std::string json();

}
