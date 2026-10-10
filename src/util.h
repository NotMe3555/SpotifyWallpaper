
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

std::wstring exeDir();
bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const std::string& data);
void deleteFile(const std::wstring& path);

std::string base64Encode(const std::string& in);
std::string base64Decode(const std::string& in);
std::string urlEncode(const std::string& s);
std::string urlDecode(const std::string& s);
std::map<std::string, std::string> parseQuery(const std::string& q);

std::string randomHex(size_t bytes);
int64_t nowMs();


std::string dpapiProtect(const std::string& plain);
bool dpapiUnprotect(const std::string& blob, std::string& plain);

std::string formatTime(int64_t ms);
