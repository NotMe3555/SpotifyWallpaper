
#pragma once
#include <string>

namespace server {


bool start(const std::wstring& publicDir, std::string& error);
void stop();

}
