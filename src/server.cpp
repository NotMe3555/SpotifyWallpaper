#include "server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "auth.h"
#include "json.h"
#include "lyrics.h"
#include "settings.h"
#include "youtube.h"
#include "util.h"

namespace server {
namespace {

SOCKET g_listen = INVALID_SOCKET;
std::wstring g_root;

const char* mimeType(const std::string& path) {
  size_t dot = path.find_last_of('.');
  std::string ext = dot == std::string::npos ? "" : path.substr(dot);
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".json") return "application/json";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".webp") return "image/webp";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

const char* statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Internal Server Error";
  }
}

void sendAll(SOCKET s, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(s, data.data() + sent, (int)(data.size() - sent), 0);
    if (n <= 0) return;
    sent += n;
  }
}

void respond(SOCKET s, int status, const std::string& body, const char* type, const std::string& extra = "") {
  std::string head = "HTTP/1.1 " + std::to_string(status) + " " + statusText(status) +
                     "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra + "\r\n";
  sendAll(s, head + body);
}

void redirect(SOCKET s, const std::string& location) {
  respond(s, 302, "", "text/plain", "Location: " + location + "\r\n");
}

std::string htmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string messagePage(const std::string& title, const std::string& text) {
  return "<!doctype html><meta charset=\"utf-8\"><title>" + htmlEscape(title) +
         "</title><body style=\"background:#0b0c0f;color:#cfd3dc;font:16px 'Segoe UI',system-ui;display:grid;"
         "place-items:center;height:100vh;margin:0\"><div style=\"max-width:520px;text-align:center;"
         "line-height:1.6\"><h2 style=\"font-weight:400\">" + htmlEscape(title) +
         "</h2><p style=\"opacity:.7\">" + htmlEscape(text) + "</p></div>";
}

void serveStatic(SOCKET s, std::string path) {
  if (path == "/") path = "/index.html";

  if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
      path.find(':') != std::string::npos)
    return respond(s, 403, "Forbidden", "text/plain");
  std::wstring file = g_root + widen(path);
  for (auto& ch : file)
    if (ch == L'/') ch = L'\\';
  std::string data;
  if (!readFile(file, data)) return respond(s, 404, "Not found", "text/plain");
  respond(s, 200, data, mimeType(path));
}

void handle(SOCKET s) {
  DWORD timeout = 5000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

  std::string req;
  char buf[4096];
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
    int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) return;
    req.append(buf, n);
  }

  size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) return respond(s, 400, "Bad request", "text/plain");
  std::string method = req.substr(0, sp1);
  std::string target = req.substr(sp1 + 1, sp2 - sp1 - 1);
  size_t q = target.find('?');
  std::string path = urlDecode(target.substr(0, q));
  auto query = parseQuery(q == std::string::npos ? "" : target.substr(q + 1));

  if (method != "GET" && method != "HEAD") return respond(s, 405, "Method not allowed", "text/plain");

  if (path == "/login") {
    if (!auth::hasCredentials())
      return respond(s, 500,
                     messagePage("Missing app credentials",
                                 "Open the Spotify Wallpaper window and enter your app's Client ID and Client "
                                 "Secret, then try again."),
                     "text/html; charset=utf-8");
    return redirect(s, auth::beginLogin());
  }

  if (path == "/callback") {
    if (!query["error"].empty())
      return respond(s, 400, messagePage("Login cancelled", "Spotify said: " + query["error"]),
                     "text/html; charset=utf-8");
    std::string error;
    if (!auth::completeLogin(query["state"], query["code"], error))
      return respond(s, 400, messagePage("Login failed", error), "text/html; charset=utf-8");
    return redirect(s, "/");
  }

  if (path == "/token") {
    int64_t expiresAt = 0;
    std::string token = auth::accessToken(&expiresAt);
    if (token.empty()) return respond(s, 401, "{\"error\":\"not_authenticated\"}", "application/json");
    return respond(s, 200,
                   "{\"access_token\":" + jsonEscape(token) + ",\"expires_at\":" + std::to_string(expiresAt) + "}",
                   "application/json");
  }

  if (path == "/settings") return respond(s, 200, settings::json(), "application/json");


  if (path == "/log") {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::wstring file = g_root + L"\\..\\video.log";
    std::string text;
    readFile(file, text);
    if (text.size() > 256 * 1024) text.clear();
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[16];
    snprintf(stamp, sizeof stamp, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    writeFile(file, text + stamp + query["msg"].substr(0, 500) + "\r\n");
    return respond(s, 200, "{}", "application/json");
  }

  if (path == "/video") {
    std::string error;
    std::string list = youtube::findVideo(query["artist"], query["title"], error), ids;
    for (size_t start = 0; start < list.size();) {
      size_t comma = list.find(',', start);
      if (comma == std::string::npos) comma = list.size();
      ids += (ids.empty() ? "" : ",") + jsonEscape(list.substr(start, comma - start));
      start = comma + 1;
    }
    return respond(s, 200, "{\"ids\":[" + ids + "],\"error\":" + jsonEscape(error) + "}", "application/json");
  }

  if (path == "/lyrics") {
    std::string json = lyrics::find(query["artist"], query["title"], query["album"], atoi(query["duration"].c_str()));
    return respond(s, 200, json, "application/json; charset=utf-8");
  }

  if (path == "/logout") {
    auth::logout();
    return redirect(s, "/");
  }

  serveStatic(s, path);
}

}

bool start(const std::wstring& publicDir, std::string& error) {
  g_root = publicDir;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    error = "Couldn't start networking";
    return false;
  }
  g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  BOOL exclusive = TRUE;
  setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof exclusive);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)auth::kPort);
  inet_pton(AF_INET, auth::kHost, &addr.sin_addr);
  if (bind(g_listen, (sockaddr*)&addr, sizeof addr) == SOCKET_ERROR || listen(g_listen, SOMAXCONN) == SOCKET_ERROR) {
    error = WSAGetLastError() == WSAEADDRINUSE ? "Port 8888 is already in use (is the old Node server running?)"
                                               : "Couldn't open port 8888";
    closesocket(g_listen);
    g_listen = INVALID_SOCKET;
    return false;
  }
  std::thread([] {
    for (;;) {
      SOCKET client = accept(g_listen, nullptr, nullptr);
      if (client == INVALID_SOCKET) {
        if (g_listen == INVALID_SOCKET) return;
        continue;
      }
      std::thread([client] {
        handle(client);
        shutdown(client, SD_SEND);
        closesocket(client);
      }).detach();
    }
  }).detach();
  return true;
}

void stop() {
  SOCKET s = g_listen;
  g_listen = INVALID_SOCKET;
  if (s != INVALID_SOCKET) closesocket(s);
}

}
