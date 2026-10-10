

#pragma once
#include <string>

namespace youtube {

void init(const std::wstring& dir);



std::string findVideo(const std::string& artist, const std::string& title, std::string& error);


std::string cleanSongTitle(const std::string& title);

}
