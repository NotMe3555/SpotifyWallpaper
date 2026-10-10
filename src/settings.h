

#pragma once
#include <string>
#include <vector>

namespace settings {

void init(const std::wstring& dir);
std::string get(const std::string& key);
bool on(const std::string& key);
std::vector<std::string> list(const std::string& key);
void set(const std::string& key, const std::string& value);
void resetLook();
std::string json();

}
