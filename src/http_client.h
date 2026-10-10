
#pragma once
#include <string>
#include <vector>

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error;
  int retryAfterSec = 0;
};

HttpResponse httpRequest(const std::wstring& method, const std::wstring& url,
                         const std::vector<std::wstring>& headers = {}, const std::string& body = {});
