

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio.h"
#include "auth.h"
#include "resource.h"
#include "server.h"
#include "settings.h"
#include "spotify.h"
#include "util.h"
#include "wallpaper.h"
#include "youtube.h"

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

constexpr wchar_t kClass[] = L"SpotifyWallpaperWindow";
constexpr wchar_t kAppName[] = L"Spotify Wallpaper";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SpotifyWallpaper";

constexpr wchar_t kWallpaperUrl[] = L"http://localhost:8888";
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;



constexpr float kBaseW = 860, kBaseH = 642, kBaseLeftW = 340;
constexpr float kMinZoom = 0.62f, kMaxZoom = 1.3f;
constexpr float kFieldH = 40;
constexpr float kContentY = 140;
constexpr float kIdFieldY = 184, kSecretFieldY = 256, kRedirectFieldY = 328, kYoutubeFieldY = 496;

enum : UINT {
  WM_APP_AUTH = WM_APP + 1, WM_APP_PLAYER, WM_APP_ART, WM_APP_TOAST, WM_APP_TRAY, WM_APP_NAME, WM_APP_SHOW,
  WM_APP_SPECTRUM
};

enum Hit { kNone, kPrev, kPlay, kNext, kEye, kCopyRedirect, kDashboard, kConnect, kCopyUrl, kOpenUrl, kReset,
           kEyeYoutube, kYoutubeLink };
constexpr int kTabHit = 100, kRowHit = 200, kHitMax = 400;
enum Tab { kTabAccount, kTabDisplay, kTabCustomize, kTabCount };
const wchar_t* const kTabNames[kTabCount] = {L"Account", L"Display", L"Customize"};
enum { kEditId = 101, kEditSecret = 102, kEditYoutube = 103, kTimerAnim = 1, kTrayId = 1 };
enum { kMenuOpen = 1, kMenuWallpaper, kMenuToggleWallpaper, kMenuQuit };


constexpr wchar_t kGlyphPlay[] = L"", kGlyphPause[] = L"", kGlyphPrev[] = L"",
                  kGlyphNext[] = L"", kGlyphCopy[] = L"", kGlyphOpen[] = L"",
                  kGlyphShow[] = L"", kGlyphHide[] = L"", kGlyphCheck[] = L"",
                  kGlyphMusic[] = L"", kGlyphLink[] = L"", kGlyphChevron[] = L"",
                  kGlyphReset[] = L"";



struct Choice {
  const char* value;
  const wchar_t* label;
};
struct Row {
  int tab;
  const char* key;
  const wchar_t* title;
  const wchar_t* sub;
  std::vector<Choice> choices;
};

const std::vector<Row> kRows = {
    {kTabDisplay, "desktop", L"Live desktop wallpaper", L"Shows the player behind your desktop icons.", {}},
    {kTabDisplay, "monitor", L"Show on", L"Which screen the wallpaper appears on.", {}},
    {kTabDisplay, "interactive", L"Clickable wallpaper", L"Click and scroll on the desktop to control playback.", {}},
    {kTabDisplay, "pauseCovered", L"Pause when covered", L"Saves power while a maximized or fullscreen app is in front.",
     {}},
    {kTabDisplay, "startup", L"Launch at Windows startup", L"Runs quietly in the tray so the wallpaper is always live.",
     {}},

    {kTabCustomize, "position", L"Layout", L"Where the player sits on screen.",
     {{"center", L"Centered"}, {"left", L"Left"}, {"right", L"Right"}, {"bottomLeft", L"Bottom left"},
      {"bottomRight", L"Bottom right"}}},
    {kTabCustomize, "scale", L"Size", L"How big the player is drawn.",
     {{"75", L"Small"}, {"90", L"Compact"}, {"100", L"Normal"}, {"115", L"Large"}, {"130", L"Extra large"},
      {"150", L"Huge"}}},
    {kTabCustomize, "accent", L"Accent color", L"Used for the equalizer, progress bar and glow.",
     {{"album", L"From album art"}, {"white", L"White"}, {"blue", L"Blue"}, {"green", L"Green"}, {"pink", L"Pink"},
      {"orange", L"Orange"}, {"purple", L"Purple"}, {"red", L"Red"}}},
    {kTabCustomize, "artShape", L"Cover shape", L"",
     {{"rounded", L"Rounded"}, {"square", L"Square"}, {"circle", L"Circle"}, {"vinyl", L"Spinning vinyl"}}},
    {kTabCustomize, "motion", L"Ambient motion", L"Drifting background and equalizer animation.", {}},

    {kTabCustomize, "background", L"Background", L"What fills the screen behind the player.",
     {{"art", L"Blurred album art"}, {"glow", L"Soft color glow"}, {"solid", L"Solid color"},
      {"gradient", L"Gradient"}, {"black", L"Pure black"}}},
    {kTabCustomize, "bgColor", L"Background color", L"Solid color, gradient start, and base under the art.", {}},
    {kTabCustomize, "bgColor2", L"Gradient end color", L"Used by the Gradient background.", {}},
    {kTabCustomize, "brightness", L"Art brightness", L"", {{"dim", L"Dim"}, {"normal", L"Normal"}, {"bright", L"Bright"}}},
    {kTabCustomize, "blur", L"Art blur", L"", {{"light", L"Light"}, {"normal", L"Normal"}, {"heavy", L"Heavy"}}},
    {kTabCustomize, "vignette", L"Vignette", L"Darkens the edges of the screen.", {}},
    {kTabCustomize, "grain", L"Film grain", L"A subtle texture over the background.", {}},

    {kTabCustomize, "video", L"Music videos", L"Plays the official video, muted and synced, when one exists.", {}},
    {kTabCustomize, "videoMode", L"Show the video", L"",
     {{"background", L"As the background"}, {"cover", L"In place of the cover"}}},
    {kTabCustomize, "videoBrightness", L"Video brightness", L"Dims the music video so the player stays readable.",
     {{"30", L"30%"}, {"45", L"45%"}, {"60", L"60%"}, {"75", L"75%"}, {"90", L"90%"}, {"100", L"100%"}}},
    {kTabCustomize, "visualizer", L"Visualizer", L"Reacts to the music playing on your PC.",
     {{"off", L"Off"}, {"ring", L"Ring around the cover"}, {"bars", L"Bars along the bottom"},
      {"both", L"Ring and bars"}}},
    {kTabCustomize, "vizIntensity", L"Visualizer intensity", L"",
     {{"low", L"Subtle"}, {"normal", L"Normal"}, {"high", L"Punchy"}}},
    {kTabCustomize, "vizColor", L"Visualizer color", L"",
     {{"accent", L"Accent color"}, {"white", L"White"}, {"rainbow", L"Rainbow"}}},

    {kTabCustomize, "lyrics", L"Lyrics", L"Synced to the song, from LRCLIB. Not every song has them.",
     {{"off", L"Off"}, {"player", L"Under the song info"}, {"screen", L"Large, across the screen"}}},
    {kTabCustomize, "lyricsSize", L"Lyrics size", L"",
     {{"small", L"Small"}, {"normal", L"Normal"}, {"large", L"Large"}}},

    {kTabCustomize, "clock", L"Clock", L"", {}},
    {kTabCustomize, "clock24", L"24-hour clock", L"", {}},
    {kTabCustomize, "date", L"Date", L"", {}},
    {kTabCustomize, "art", L"Album art", L"", {}},
    {kTabCustomize, "album", L"Album name", L"", {}},
    {kTabCustomize, "progress", L"Progress bar", L"", {}},
    {kTabCustomize, "controls", L"Playback buttons", L"", {}},
    {kTabCustomize, "volume", L"Volume slider", L"", {}},
    {kTabCustomize, "device", L"“Listening on” device", L"", {}},
    {kTabCustomize, "idleFade", L"Fade controls when the mouse is idle", L"", {}},

};


struct Section {
  const char* firstKey;
  const wchar_t* label;
  bool compact;
};
const Section kSections[] = {
    {"position", L"LOOK", false},
    {"background", L"BACKDROP", false},
    {"video", L"VIDEO & VISUALIZER", false},
    {"lyrics", L"LYRICS", false},
    {"clock", L"ON-SCREEN ELEMENTS", true},
};



D2D1_COLOR_F hex(uint32_t rgb, float a = 1) {
  return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a};
}
D2D1_COLOR_F alpha(D2D1_COLOR_F c, float a) { c.a *= a; return c; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}
float ease(float t) { return t * t * (3 - 2 * t); }
float approach(float v, float target, float step) {
  return v < target ? std::min(target, v + step) : std::max(target, v - step);
}
D2D1_RECT_F rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
D2D1_RECT_F inset(D2D1_RECT_F r, float d) { return {r.left + d, r.top + d, r.right - d, r.bottom - d}; }
bool contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
float seconds(Clock::duration d) { return std::chrono::duration<float>(d).count(); }

const D2D1_COLOR_F kBg = hex(0x101116), kLeftBg = hex(0x0A0B0E), kText = hex(0xEEF0F4),
                   kText2 = hex(0xEEF0F4, .64f), kText3 = hex(0xEEF0F4, .40f), kLine = hex(0xFFFFFF, .08f),
                   kField = hex(0x181A20), kGreen = hex(0x4ADE80), kAmber = hex(0xFBBF24), kRed = hex(0xF87171),
                   kDefaultAccent = {0.67f, 0.73f, 0.82f, 1};
const COLORREF kFieldRef = RGB(0x18, 0x1A, 0x20), kTextRef = RGB(0xEE, 0xF0, 0xF4);

struct App {
  HINSTANCE inst{};
  HWND hwnd{}, editId{}, editSecret{}, editYoutube{};
  UINT dpi = 96;
  float scale = 1;
  HFONT editFont{};
  float editFontUi = 0;
  HBRUSH fieldBrush{};
  int editLineH = 20;
  UINT taskbarCreatedMsg = 0;


  float zoom = 1, ui = 1;
  float W = kBaseW, H = kBaseH, leftW = kBaseLeftW, R0 = kBaseLeftW + 28, R1 = kBaseW - 28;

  ComPtr<ID2D1Factory1> d2d;
  ComPtr<IDWriteFactory> dw;
  ComPtr<ID2D1DCRenderTarget> rt;
  ComPtr<ID2D1DeviceContext> dc;
  ComPtr<ID2D1SolidColorBrush> brush;
  ComPtr<ID2D1Effect> blur[2];
  ComPtr<IDWriteTextFormat> fTitle, fTrack, fSub, fLabel, fFieldLabel, fBody, fSmall, fButton, fTab, fIcon, fIconSm,
      fIconLg, fIconXL;
  std::wstring uiFont, iconFont;

  struct Hot { D2D1_RECT_F rc; int id; };
  std::vector<Hot> hots;
  float hotOffsetY = 0;
  D2D1_RECT_F hotClip = {-1e9f, -1e9f, 1e9f, 1e9f};
  float scrollY = 0, scrollTarget = 0, scrollMax = 0;
  int hover = kNone, pressed = kNone, focusedEdit = 0;
  bool trackingMouse = false;
  float anim[kHitMax] = {};
  std::vector<float> rowOn;
  int tab = kTabAccount;
  float tabAnim = 0;
  bool secretVisible = false, youtubeVisible = false;

  spotify::Player player;
  Clock::time_point syncedAt, playLockUntil, start, lastTick;
  std::shared_ptr<spotify::Art> art, prevArt;
  ComPtr<ID2D1Bitmap> artBmp, prevArtBmp;
  float artFade = 1;

  bool connected = false;
  std::wstring displayName;
  std::string serverError;
  std::wstring toast;
  Clock::time_point toastAt, copiedAt;
  int copiedId = kNone;
  bool startup = false;
  UINT timerInterval = 0;
  bool trayHintShown = false;

  HANDLE wake{};
  std::atomic<bool> quitting{false};
} g;



std::wstring exePath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  return std::wstring(buf, n);
}

bool startupEnabled() {
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
         ERROR_SUCCESS;
}

bool setStartup(bool on) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
  LSTATUS rc;
  if (on) {
    std::wstring cmd = L"\"" + exePath() + L"\" --tray";
    rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * 2));
  } else {
    rc = RegDeleteValueW(key, kRunValue);
    if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return rc == ERROR_SUCCESS;
}

void copyToClipboard(const std::wstring& text) {
  if (!OpenClipboard(g.hwnd)) return;
  EmptyClipboard();
  size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    memcpy(GlobalLock(mem), text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
  }
  CloseClipboard();
}

void openUrl(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string editText(HWND e) {
  int n = GetWindowTextLengthW(e);
  std::wstring w(n, L'\0');
  GetWindowTextW(e, w.data(), n + 1);
  std::string s = narrow(w);
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}


void enableDarkMenus() {
  HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!ux) return;
  using SetPreferredAppMode = int(WINAPI*)(int);
  using FlushMenuThemes = void(WINAPI*)();
  auto setMode = (SetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
  auto flush = (FlushMenuThemes)GetProcAddress(ux, MAKEINTRESOURCEA(136));
  if (setMode) setMode(2 );
  if (flush) flush();
}



bool isColorRow(const Row& r) { return std::string(r.key) == "bgColor" || std::string(r.key) == "bgColor2"; }
bool isSwitchRow(const Row& r) { return r.choices.empty() && !isColorRow(r) && std::string(r.key) != "monitor"; }

COLORREF parseHex(const std::string& hex) {
  unsigned v = 0;
  if (hex.size() == 7 && hex[0] == '#') sscanf_s(hex.c_str() + 1, "%06x", &v);
  return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

std::string toHex(COLORREF c) {
  char buf[8];
  snprintf(buf, sizeof buf, "#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
  return buf;
}

struct ColorPreset {
  const char* hex;
  const wchar_t* name;
};
const ColorPreset kColorPresets[] = {
    {"#000000", L"Black"},       {"#07080b", L"Night"},  {"#16181d", L"Charcoal"}, {"#0b1026", L"Midnight blue"},
    {"#1d1033", L"Deep purple"}, {"#0c1f17", L"Forest"}, {"#2a0d17", L"Wine"},     {"#072431", L"Ocean"},
    {"#2b1a0a", L"Espresso"},    {"#3a3f4b", L"Slate"},
};

std::string rowValue(const Row& r) {
  if (std::string(r.key) == "startup") return g.startup ? "on" : "off";
  return settings::get(r.key);
}

std::vector<Choice> monitorChoices(std::vector<std::wstring>& labels) {
  auto list = wallpaper::monitors();
  static std::vector<std::string> ids;
  ids.clear();
  labels.clear();
  ids.push_back("primary");
  labels.push_back(L"Main monitor");
  ids.push_back("all");
  labels.push_back(L"All monitors");
  for (auto& m : list) {
    ids.push_back(m.id);
    labels.push_back(m.label);
  }
  std::vector<Choice> out;
  for (size_t i = 0; i < ids.size(); ++i) out.push_back({ids[i].c_str(), labels[i].c_str()});
  return out;
}

std::wstring choiceLabel(const Row& r) {
  std::string v = rowValue(r);
  if (isColorRow(r)) {
    for (auto& p : kColorPresets)
      if (v == p.hex) return p.name;
    std::wstring w = widen(v);
    CharUpperBuffW(w.data(), (DWORD)w.size());
    return w;
  }
  std::vector<std::wstring> labels;
  std::vector<Choice> choices = std::string(r.key) == "monitor" ? monitorChoices(labels) : r.choices;
  for (auto& c : choices)
    if (v == c.value) return c.label;
  return std::string(r.key) == "monitor" ? L"Disconnected monitor" : widen(v);
}

wallpaper::Options wallpaperOptions() {
  wallpaper::Options o;
  o.monitor = settings::get("monitor");
  o.interactive = settings::on("interactive");
  o.pauseCovered = settings::on("pauseCovered");
  return o;
}

void pushLookToWallpaper() {
  wallpaper::runScript(L"window.applySettings && window.applySettings(" + widen(settings::json()) + L")");
}



bool isVisible() { return IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd); }
bool toastActive() { return !g.toast.empty() && seconds(Clock::now() - g.toastAt) < 3.6f; }
bool copiedActive() { return g.copiedId != kNone && seconds(Clock::now() - g.copiedAt) < 1.6f; }

bool transitioning() {
  for (int i = 1; i < kHitMax; ++i)
    if (g.anim[i] != (g.hover == i ? 1.f : 0.f)) return true;
  for (size_t i = 0; i < kRows.size(); ++i)
    if (g.rowOn[i] != (rowValue(kRows[i]) == "on" ? 1.f : 0.f)) return true;
  return g.tabAnim != (float)g.tab || g.scrollY != g.scrollTarget || g.artFade < 1 || toastActive() ||
         copiedActive();
}

void updateTimer() {
  UINT interval = 0;
  if (isVisible()) interval = transitioning() ? 16 : (g.player.hasItem && g.player.playing) ? 33 : 0;
  if (interval == g.timerInterval) return;
  if (!g.timerInterval) g.lastTick = Clock::now();
  g.timerInterval = interval;
  if (interval) SetTimer(g.hwnd, kTimerAnim, interval, nullptr);
  else KillTimer(g.hwnd, kTimerAnim);
}

void invalidate() {
  InvalidateRect(g.hwnd, nullptr, FALSE);
  updateTimer();
}

void showToast(const std::wstring& msg) {
  g.toast = msg;
  g.toastAt = Clock::now();
  invalidate();
}



ComPtr<IDWriteTextFormat> makeFormat(const std::wstring& family, float size, DWRITE_FONT_WEIGHT weight) {
  ComPtr<IDWriteTextFormat> f;
  g.dw->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                         L"en-us", &f);
  f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  ComPtr<IDWriteInlineObject> ellipsis;
  g.dw->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
  f->SetTrimming(&trim, ellipsis.Get());
  return f;
}

std::wstring pickFamily(std::initializer_list<const wchar_t*> names) {
  ComPtr<IDWriteFontCollection> fonts;
  g.dw->GetSystemFontCollection(&fonts);
  for (auto name : names) {
    UINT32 index;
    BOOL exists = FALSE;
    if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) return name;
  }
  return *(names.end() - 1);
}

void createFormats() {
  g.uiFont = pickFamily({L"Segoe UI Variable Text", L"Segoe UI"});
  std::wstring display = pickFamily({L"Segoe UI Variable Display", L"Segoe UI"});
  g.iconFont = pickFamily({L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});
  g.fTitle = makeFormat(display, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  g.fTrack = makeFormat(display, 19, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  g.fSub = makeFormat(g.uiFont, 13.5f, DWRITE_FONT_WEIGHT_NORMAL);
  g.fLabel = makeFormat(g.uiFont, 11, DWRITE_FONT_WEIGHT_BOLD);
  g.fFieldLabel = makeFormat(g.uiFont, 12.5f, DWRITE_FONT_WEIGHT_NORMAL);
  g.fBody = makeFormat(g.uiFont, 14, DWRITE_FONT_WEIGHT_NORMAL);
  g.fSmall = makeFormat(g.uiFont, 12, DWRITE_FONT_WEIGHT_NORMAL);
  g.fButton = makeFormat(g.uiFont, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  g.fTab = makeFormat(g.uiFont, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  g.fIcon = makeFormat(g.iconFont, 15, DWRITE_FONT_WEIGHT_NORMAL);
  g.fIconSm = makeFormat(g.iconFont, 10, DWRITE_FONT_WEIGHT_NORMAL);
  g.fIconLg = makeFormat(g.iconFont, 20, DWRITE_FONT_WEIGHT_NORMAL);
  g.fIconXL = makeFormat(g.iconFont, 44, DWRITE_FONT_WEIGHT_NORMAL);
}

void discardTarget() {
  g.rt.Reset();
  g.dc.Reset();
  g.brush.Reset();
  g.blur[0].Reset();
  g.blur[1].Reset();
  g.artBmp.Reset();
  g.prevArtBmp.Reset();
}

bool ensureTarget() {
  if (g.rt) return true;
  auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  if (FAILED(g.d2d->CreateDCRenderTarget(&props, &g.rt))) return false;
  g.rt.As(&g.dc);
  g.rt->CreateSolidColorBrush(kText, &g.brush);
  if (g.dc)
    for (auto& b : g.blur)
      if (SUCCEEDED(g.dc->CreateEffect(CLSID_D2D1GaussianBlur, &b))) {
        b->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 22.f);
        b->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        b->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
      }
  return true;
}

ID2D1Bitmap* bitmapFor(const std::shared_ptr<spotify::Art>& art, ComPtr<ID2D1Bitmap>& slot) {
  if (!art) return nullptr;
  if (!slot)
    g.rt->CreateBitmap(D2D1::SizeU(art->width, art->height), art->pixels.data(), art->width * 4,
                       D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                D2D1_ALPHA_MODE_PREMULTIPLIED)),
                       &slot);
  return slot.Get();
}



void fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
  g.brush->SetColor(c);
  g.rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), g.brush.Get());
}

void strokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width = 1) {
  g.brush->SetColor(c);
  g.rt->DrawRoundedRectangle(D2D1::RoundedRect(inset(r, width / 2), radius, radius), g.brush.Get(), width);
}

void fillCircle(float cx, float cy, float r, D2D1_COLOR_F c) {
  g.brush->SetColor(c);
  g.rt->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), g.brush.Get());
}

void text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, D2D1_COLOR_F c,
          DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
  f->SetTextAlignment(align);
  g.brush->SetColor(c);
  g.rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, g.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float measure(const std::wstring& s, IDWriteTextFormat* f) {
  ComPtr<IDWriteTextLayout> layout;
  f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  if (FAILED(g.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 2000, 100, &layout))) return 0;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

void drawBitmapRounded(ID2D1Bitmap* bmp, D2D1_RECT_F r, float radius, float opacity) {
  if (!bmp || opacity <= 0) return;
  D2D1_SIZE_F sz = bmp->GetSize();
  ComPtr<ID2D1BitmapBrush> bb;
  if (g.dc) {
    ComPtr<ID2D1BitmapBrush1> bb1;
    g.dc->CreateBitmapBrush(bmp,
                            D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                                         D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC),
                            D2D1::BrushProperties(opacity), &bb1);
    bb = bb1;
  } else {
    g.rt->CreateBitmapBrush(bmp, D2D1::BitmapBrushProperties(), D2D1::BrushProperties(opacity), &bb);
  }
  if (!bb) return;
  bb->SetTransform(D2D1::Matrix3x2F::Scale((r.right - r.left) / sz.width, (r.bottom - r.top) / sz.height) *
                   D2D1::Matrix3x2F::Translation(r.left, r.top));
  g.rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), bb.Get());
}


void drawBackdrop(ID2D1Bitmap* bmp, ID2D1Effect* blur, float opacity) {
  if (!bmp || !blur || !g.dc || opacity <= 0) return;
  D2D1_SIZE_F sz = bmp->GetSize();
  float s = std::max(g.leftW / sz.width, g.H / sz.height) * 1.25f;
  blur->SetInput(0, bmp);
  g.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                         D2D1::IdentityMatrix(), opacity),
                  nullptr);
  g.dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                     D2D1::Matrix3x2F::Translation((g.leftW - sz.width * s) / 2, (g.H - sz.height * s) / 2));
  g.dc->DrawImage(blur);
  g.dc->SetTransform(D2D1::Matrix3x2F::Identity());
  g.dc->PopLayer();
}

void addHot(D2D1_RECT_F r, int id) {
  r.top = std::max(r.top + g.hotOffsetY, g.hotClip.top);
  r.bottom = std::min(r.bottom + g.hotOffsetY, g.hotClip.bottom);
  if (r.bottom > r.top) g.hots.push_back({r, id});
}

void iconButton(int id, D2D1_RECT_F r, const wchar_t* glyph, bool enabled = true) {
  float h = ease(g.anim[id]);
  if (enabled) addHot(r, id);
  bool copied = g.copiedId == id && copiedActive();
  float radius = std::min(r.right - r.left, r.bottom - r.top) / 2;
  if (enabled && h > 0.01f) fillRound(r, radius, hex(0xFFFFFF, (g.pressed == id ? 0.14f : 0.08f) * h));
  D2D1_COLOR_F c = !enabled ? alpha(kText2, .35f) : copied ? kGreen : mix(kText2, kText, h);
  text(copied ? kGlyphCheck : glyph, r, g.fIcon.Get(), c, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void field(D2D1_RECT_F r, bool focused, D2D1_COLOR_F accent) {
  fillRound(r, 10, kField);
  strokeRound(r, 10, focused ? alpha(accent, .9f) : kLine, focused ? 1.5f : 1);
}

void sectionLabel(const wchar_t* label, float y) { text(label, {g.R0, y, g.R1, y + 16}, g.fLabel.Get(), kText3); }

void switchKnob(float right, float cy, float on, float hover, D2D1_COLOR_F accent) {
  float k = ease(on);
  D2D1_RECT_F track = {right - 44, cy - 11, right, cy + 11};
  fillRound(track, 11, mix(hex(0xFFFFFF, .14f + .04f * hover), accent, k));
  fillCircle(track.left + 11 + 22 * k, cy, 8, mix(hex(0xC9CDD6), hex(0x0B0C0F), k));
}



D2D1_COLOR_F currentAccent() {
  D2D1_COLOR_F to = kDefaultAccent, from = kDefaultAccent;
  if (g.art) to = {g.art->accent[0], g.art->accent[1], g.art->accent[2], 1};
  if (g.prevArt) from = {g.prevArt->accent[0], g.prevArt->accent[1], g.prevArt->accent[2], 1};
  return mix(from, to, ease(g.artFade));
}

int64_t currentProgress() {
  int64_t pos = g.player.progressMs;
  if (g.player.playing)
    pos += std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.syncedAt).count();
  return std::clamp<int64_t>(pos, 0, g.player.durationMs);
}

void drawNowPlaying(D2D1_COLOR_F accent, float t) {
  const float W = g.leftW, H = g.H, cx = W / 2;
  g.rt->PushAxisAlignedClip({0, 0, W, H}, D2D1_ANTIALIAS_MODE_ALIASED);
  g.brush->SetColor(kLeftBg);
  g.rt->FillRectangle({0, 0, W, H}, g.brush.Get());


  float fade = ease(g.artFade);
  drawBackdrop(bitmapFor(g.prevArt, g.prevArtBmp), g.blur[1].Get(), 0.55f * (1 - fade));
  drawBackdrop(bitmapFor(g.art, g.artBmp), g.blur[0].Get(), 0.55f * fade);
  float oy = std::max(0.f, (H - kBaseH) / 2);
  {
    D2D1_GRADIENT_STOP stops[] = {{0, alpha(accent, .16f)}, {1, alpha(accent, 0)}};
    ComPtr<ID2D1GradientStopCollection> coll;
    ComPtr<ID2D1RadialGradientBrush> glow;
    g.rt->CreateGradientStopCollection(stops, 2, &coll);
    if (coll && SUCCEEDED(g.rt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties({cx, 190 + oy}, {0, 0}, 230, 210), coll.Get(), &glow)))
      g.rt->FillRectangle({0, 0, W, H}, glow.Get());

    D2D1_GRADIENT_STOP shade[] = {{0, alpha(kLeftBg, .30f)}, {0.55f, alpha(kLeftBg, .45f)}, {1, alpha(kLeftBg, .92f)}};
    ComPtr<ID2D1GradientStopCollection> shadeColl;
    ComPtr<ID2D1LinearGradientBrush> shadeBrush;
    g.rt->CreateGradientStopCollection(shade, 3, &shadeColl);
    if (shadeColl && SUCCEEDED(g.rt->CreateLinearGradientBrush(
                         D2D1::LinearGradientBrushProperties({0, 0}, {0, H}), shadeColl.Get(), &shadeBrush)))
      g.rt->FillRectangle({0, 0, W, H}, shadeBrush.Get());
  }

  g.rt->SetTransform(D2D1::Matrix3x2F::Translation(0, oy));
  g.hotOffsetY = oy;
  const auto& p = g.player;
  bool has = g.connected && p.hasItem;


  std::wstring eyebrow = !g.connected ? L"NOT CONNECTED" : !p.hasItem ? L"NOTHING PLAYING"
                         : p.playing   ? L"NOW PLAYING"
                                       : L"PAUSED";
  float ew = measure(eyebrow, g.fLabel.Get());
  float ex = cx - (ew + 18) / 2;
  for (int i = 0; i < 3; ++i) {
    float level = has && p.playing ? 0.25f + 0.75f * std::fabs(std::sin(t * (2.6f + i * 0.7f) + i * 1.3f)) : 0.3f;
    float bh = 11 * level;
    g.brush->SetColor(accent);
    g.rt->FillRectangle({ex + i * 4.5f, 46 - bh, ex + i * 4.5f + 2.5f, 46}, g.brush.Get());
  }
  text(eyebrow, {ex + 18, 32, ex + 18 + ew + 4, 48}, g.fLabel.Get(), accent);


  D2D1_RECT_F art = rect(cx - 118, 68, 236, 236);
  for (int i = 1; i <= 10; ++i) {
    D2D1_RECT_F s = {art.left - i * 1.6f, art.top + i * 1.4f, art.right + i * 1.6f, art.bottom + i * 2.6f};
    fillRound(s, 14 + i * 1.6f, hex(0x000000, 0.045f));
  }
  fillRound(art, 14, hex(0x1A1C22));
  if (!g.art && !g.prevArt) text(kGlyphMusic, art, g.fIconXL.Get(), kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
  drawBitmapRounded(bitmapFor(g.prevArt, g.prevArtBmp), art, 14, 1 - fade);
  drawBitmapRounded(bitmapFor(g.art, g.artBmp), art, 14, fade);
  strokeRound(art, 14, hex(0xFFFFFF, .06f));


  std::wstring title = !g.connected ? L"Connect your account" : has ? widen(p.title) : L"Quiet for now";
  std::wstring artist = !g.connected ? L"Add your app details in the Account tab"
                        : has        ? widen(p.artist)
                                     : L"Start something on Spotify";
  text(title, {24, 322, W - 24, 350}, g.fTrack.Get(), kText, DWRITE_TEXT_ALIGNMENT_CENTER);
  text(artist, {24, 351, W - 24, 372}, g.fSub.Get(), kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
  if (has && !p.album.empty())
    text(widen(p.album), {24, 372, W - 24, 392}, g.fSmall.Get(), kText3, DWRITE_TEXT_ALIGNMENT_CENTER);


  float bx0 = cx - 118, bx1 = cx + 118, by = 420;
  fillRound({bx0, by, bx1, by + 3}, 1.5f, hex(0xFFFFFF, .10f));
  if (has && p.durationMs > 0) {
    int64_t pos = currentProgress();
    float fx = bx0 + (bx1 - bx0) * (float)pos / (float)p.durationMs;
    if (fx > bx0 + 1) {
      D2D1_GRADIENT_STOP stops[] = {{0, alpha(accent, .55f)}, {1, accent}};
      ComPtr<ID2D1GradientStopCollection> coll;
      ComPtr<ID2D1LinearGradientBrush> fill;
      g.rt->CreateGradientStopCollection(stops, 2, &coll);
      if (coll && SUCCEEDED(g.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({bx0, 0}, {fx, 0}),
                                                            coll.Get(), &fill)))
        g.rt->FillRoundedRectangle(D2D1::RoundedRect({bx0, by, fx, by + 3}, 1.5f, 1.5f), fill.Get());
    }
    text(widen(formatTime(pos)), {bx0, by + 8, bx0 + 60, by + 24}, g.fSmall.Get(), kText3);
    text(widen(formatTime(p.durationMs)), {bx1 - 60, by + 8, bx1, by + 24}, g.fSmall.Get(), kText3,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
  }


  float ty = 478;
  iconButton(kPrev, rect(cx - 86, ty - 20, 40, 40), kGlyphPrev, has);
  iconButton(kNext, rect(cx + 46, ty - 20, 40, 40), kGlyphNext, has);
  D2D1_RECT_F play = rect(cx - 27, ty - 27, 54, 54);
  float h = ease(g.anim[kPlay]);
  if (has) addHot(play, kPlay);
  D2D1_RECT_F playDraw = inset(play, has ? -h * 1.5f : 0);
  fillRound(playDraw, (playDraw.right - playDraw.left) / 2,
            has ? mix(hex(0xE9ECF2), hex(0xFFFFFF), h) : hex(0xFFFFFF, .07f));
  text(has && p.playing ? kGlyphPause : kGlyphPlay, play, g.fIconLg.Get(), has ? hex(0x0B0C0F) : alpha(kText2, .35f),
       DWRITE_TEXT_ALIGNMENT_CENTER);

  if (has && !p.device.empty())
    text(L"Listening on " + widen(p.device), {24, 528, W - 24, 546}, g.fSmall.Get(), kText3,
         DWRITE_TEXT_ALIGNMENT_CENTER);

  g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
  g.hotOffsetY = 0;


  if (toastActive()) {
    float age = seconds(Clock::now() - g.toastAt);
    float a = ease(std::clamp(std::min(age / 0.2f, (3.6f - age) / 0.4f), 0.f, 1.f));
    float tw = std::min(measure(g.toast, g.fSmall.Get()) + 36, W - 32);
    D2D1_RECT_F pill = rect(cx - tw / 2, H - 62 + (1 - a) * 8, tw, 34);
    fillRound(pill, 17, hex(0x24272F, .96f * a));
    strokeRound(pill, 17, hex(0xFFFFFF, .10f * a));
    text(g.toast, inset(pill, 4), g.fSmall.Get(), alpha(kText, a), DWRITE_TEXT_ALIGNMENT_CENTER);
  }

  g.rt->PopAxisAlignedClip();
  g.brush->SetColor(hex(0xFFFFFF, .06f));
  g.rt->FillRectangle({W - 1, 0, W, H}, g.brush.Get());
}



D2D1_RECT_F tabRect(int i) {
  float w = (g.R1 - g.R0 - 8) / (float)kTabCount;
  return {g.R0 + 4 + i * w, 92, g.R0 + 4 + (i + 1) * w, 120};
}

void drawTabs(D2D1_COLOR_F accent) {
  fillRound({g.R0, 88, g.R1, 124}, 12, hex(0xFFFFFF, .04f));
  strokeRound({g.R0, 88, g.R1, 124}, 12, kLine);

  D2D1_RECT_F a = tabRect(0);
  float w = a.right - a.left, x = a.left + w * g.tabAnim;
  fillRound({x, a.top, x + w, a.bottom}, 9, hex(0xFFFFFF, .10f));
  for (int i = 0; i < kTabCount; ++i) {
    D2D1_RECT_F r = tabRect(i);
    addHot(r, kTabHit + i);
    float h = ease(g.anim[kTabHit + i]);
    D2D1_COLOR_F c = i == g.tab ? kText : mix(kText3, kText2, h);
    text(kTabNames[i], r, g.fTab.Get(), c, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  (void)accent;
}

void drawRow(size_t index, float y, float height, D2D1_COLOR_F accent) {
  const Row& r = kRows[index];
  int id = kRowHit + (int)index;
  D2D1_RECT_F row = {g.R0 - 10, y - 4, g.R1 + 10, y + height + 4};
  addHot(row, id);
  float h = ease(g.anim[id]);
  if (h > 0.01f) fillRound(row, 10, hex(0xFFFFFF, .035f * h));
  bool hasSub = r.sub && *r.sub;
  float right = g.R1;
  if (isSwitchRow(r)) {
    switchKnob(right, y + height / 2, g.rowOn[index], h, accent);
    right -= 60;
  } else {
    std::wstring label = choiceLabel(r);
    bool color = isColorRow(r);
    float swatch = color ? 22.f : 0.f;
    float lw = std::min(measure(label, g.fSmall.Get()), (g.R1 - g.R0) * 0.5f);
    D2D1_RECT_F pill = {g.R1 - lw - 40 - swatch, y + height / 2 - 15, g.R1, y + height / 2 + 15};
    fillRound(pill, 9, mix(hex(0xFFFFFF, .05f), hex(0xFFFFFF, .09f), h));
    strokeRound(pill, 9, kLine);
    if (color) {
      COLORREF c = parseHex(rowValue(r));
      D2D1_COLOR_F cc = {GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1};
      float cy = (pill.top + pill.bottom) / 2;
      fillCircle(pill.left + 18, cy, 7, cc);
      g.brush->SetColor(hex(0xFFFFFF, .35f));
      g.rt->DrawEllipse(D2D1::Ellipse({pill.left + 18, cy}, 7, 7), g.brush.Get(), 1);
    }
    text(label, {pill.left + 12 + swatch, pill.top, pill.right - 26, pill.bottom}, g.fSmall.Get(), kText);
    text(kGlyphChevron, {pill.right - 24, pill.top, pill.right - 8, pill.bottom}, g.fIconSm.Get(), kText2,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    right = pill.left - 12;
  }
  if (hasSub) {
    text(r.title, {g.R0, y, right, y + 22}, g.fBody.Get(), mix(kText2, kText, h));
    text(r.sub, {g.R0, y + 20, right, y + 40}, g.fSmall.Get(), kText3);
  } else {
    text(r.title, {g.R0, y, right, y + height}, g.fBody.Get(), mix(kText2, kText, h));
  }
}

void drawAccountTab(D2D1_COLOR_F accent) {
  const float R0 = g.R0, R1 = g.R1;
  sectionLabel(L"SPOTIFY APP", kContentY);
  {
    std::wstring link = L"Developer dashboard";
    float lw = measure(link, g.fSmall.Get());
    D2D1_RECT_F r = {R1 - lw - 20, kContentY - 4, R1, kContentY + 18};
    float h = ease(g.anim[kDashboard]);
    addHot(r, kDashboard);
    D2D1_COLOR_F c = mix(alpha(accent, .85f), accent, h);
    text(link, {r.left, r.top, r.right - 18, r.bottom}, g.fSmall.Get(), c);
    text(kGlyphOpen, {r.right - 14, r.top, r.right, r.bottom}, g.fIcon.Get(), c, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (h > 0.01f) {
      g.brush->SetColor(alpha(c, .6f * h));
      g.rt->FillRectangle({r.left, r.bottom - 3, r.right - 18, r.bottom - 2}, g.brush.Get());
    }
  }

  text(L"Client ID", {R0, kIdFieldY - 22, R1, kIdFieldY - 4}, g.fFieldLabel.Get(), kText2);
  field({R0, kIdFieldY, R1, kIdFieldY + kFieldH}, g.focusedEdit == kEditId, accent);

  text(L"Client Secret", {R0, kSecretFieldY - 22, R1, kSecretFieldY - 4}, g.fFieldLabel.Get(), kText2);
  field({R0, kSecretFieldY, R1, kSecretFieldY + kFieldH}, g.focusedEdit == kEditSecret, accent);
  iconButton(kEye, rect(R1 - 38, kSecretFieldY + 4, 32, 32), g.secretVisible ? kGlyphHide : kGlyphShow);

  text(L"Redirect URI", {R0, kRedirectFieldY - 22, R1, kRedirectFieldY - 4}, g.fFieldLabel.Get(), kText2);
  D2D1_RECT_F redirect = {R0, kRedirectFieldY, R1, kRedirectFieldY + kFieldH};
  fillRound(redirect, 10, hex(0xFFFFFF, .025f));
  strokeRound(redirect, 10, kLine);
  text(widen(auth::kRedirectUri), {R0 + 14, redirect.top, R1 - 44, redirect.bottom}, g.fBody.Get(), kText2);
  iconButton(kCopyRedirect, rect(R1 - 38, kRedirectFieldY + 4, 32, 32), kGlyphCopy);
  text(L"Add this exact address under Redirect URIs in your Spotify app.",
       {R0, kRedirectFieldY + kFieldH + 6, R1, kRedirectFieldY + kFieldH + 24}, g.fSmall.Get(), kText3);


  D2D1_RECT_F btn = {R0, 408, R1, 452};
  float h = ease(g.anim[kConnect]);
  addHot(btn, kConnect);
  if (g.connected) {
    fillRound(btn, 12, hex(0xFFFFFF, .04f + .04f * h));
    strokeRound(btn, 12, hex(0xFFFFFF, .12f + .06f * h));
    text(L"Disconnect", btn, g.fButton.Get(), mix(kText2, kText, h), DWRITE_TEXT_ALIGNMENT_CENTER);
  } else {
    fillRound(btn, 12, mix(hex(0xE9ECF2), hex(0xFFFFFF), h));
    text(L"Connect Spotify", btn, g.fButton.Get(), hex(0x0B0C0F), DWRITE_TEXT_ALIGNMENT_CENTER);
  }


  sectionLabel(L"YOUTUBE (OPTIONAL)", kYoutubeFieldY - 24);
  {
    std::wstring link = L"Get a free API key";
    float lw = measure(link, g.fSmall.Get());
    D2D1_RECT_F r = {R1 - lw - 20, kYoutubeFieldY - 28, R1, kYoutubeFieldY - 6};
    float lh = ease(g.anim[kYoutubeLink]);
    addHot(r, kYoutubeLink);
    D2D1_COLOR_F c = mix(alpha(accent, .85f), accent, lh);
    text(link, {r.left, r.top, r.right - 18, r.bottom}, g.fSmall.Get(), c);
    text(kGlyphOpen, {r.right - 14, r.top, r.right, r.bottom}, g.fIcon.Get(), c, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  field({R0, kYoutubeFieldY, R1, kYoutubeFieldY + kFieldH}, g.focusedEdit == kEditYoutube, accent);
  iconButton(kEyeYoutube, rect(R1 - 38, kYoutubeFieldY + 4, 32, 32), g.youtubeVisible ? kGlyphHide : kGlyphShow);
  text(L"Lets the wallpaper find official music videos (Customize tab).",
       {R0, kYoutubeFieldY + kFieldH + 6, R1, kYoutubeFieldY + kFieldH + 24}, g.fSmall.Get(), kText3);
}


void drawWebAddress(float y) {
  const float R0 = g.R0, R1 = g.R1;
  sectionLabel(L"WEB ADDRESS", y);
  float fy = y + 24;
  D2D1_RECT_F url = {R0, fy, R1, fy + kFieldH};
  fillRound(url, 10, hex(0xFFFFFF, .025f));
  strokeRound(url, 10, kLine);
  text(kGlyphLink, {R0 + 12, url.top, R0 + 30, url.bottom}, g.fIcon.Get(), kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
  text(kWallpaperUrl, {R0 + 38, url.top, R0 + 210, url.bottom}, g.fBody.Get(), kText);
  text(L"for Lively", {R0 + 200, url.top, R1 - 80, url.bottom}, g.fSmall.Get(), kText3,
       DWRITE_TEXT_ALIGNMENT_TRAILING);
  iconButton(kCopyUrl, rect(R1 - 74, fy + 4, 32, 32), kGlyphCopy);
  iconButton(kOpenUrl, rect(R1 - 38, fy + 4, 32, 32), kGlyphOpen);
}

void drawCustomizeTab(D2D1_COLOR_F accent) {
  const float R0 = g.R0, R1 = g.R1;
  D2D1_RECT_F view = {R0 - 12, kContentY - 10, R1 + 20, g.H - 44};
  g.rt->PushAxisAlignedClip(view, D2D1_ANTIALIAS_MODE_ALIASED);
  g.rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -g.scrollY));
  g.hotOffsetY = -g.scrollY;
  g.hotClip = view;

  float y = kContentY;
  bool compact = false;
  for (size_t i = 0; i < kRows.size(); ++i) {
    if (kRows[i].tab != kTabCustomize) continue;
    for (const Section& s : kSections) {
      if (std::string(s.firstKey) != kRows[i].key) continue;
      if (y > kContentY) y += 14;
      sectionLabel(s.label, y);
      y += 26;
      compact = s.compact;
    }
    if (!strcmp(kRows[i].key, "visualizer") && settings::on("video") && auth::youtubeKey().empty()) {
      text(L"Music videos need a YouTube API key — add one in the Account tab.", {R0, y - 6, R1, y + 14},
           g.fSmall.Get(), kAmber);
      y += 22;
    }
    drawRow(i, y, compact ? 30.f : 40.f, accent);
    y += compact ? 40.f : 52.f;
  }
  if (settings::get("visualizer") != "off") {
    text(L"The visualizer listens to everything your PC is playing.", {R0, y, R1, y + 20}, g.fSmall.Get(), kText3);
    y += 26;
  }
  std::wstring reset = L"Reset everything here to defaults";
  float lw = measure(reset, g.fSmall.Get());
  D2D1_RECT_F r = {R1 - lw - 22, y + 2, R1, y + 24};
  float h = ease(g.anim[kReset]);
  addHot(r, kReset);
  D2D1_COLOR_F c = mix(kText3, kText2, h);
  text(kGlyphReset, {r.left, r.top, r.left + 14, r.bottom}, g.fIconSm.Get(), c, DWRITE_TEXT_ALIGNMENT_CENTER);
  text(reset, {r.left + 20, r.top, r.right, r.bottom}, g.fSmall.Get(), c);
  y += 36;

  g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
  g.hotOffsetY = 0;
  g.hotClip = {-1e9f, -1e9f, 1e9f, 1e9f};


  float viewH = view.bottom - view.top;
  g.scrollMax = std::max(0.f, y - kContentY + 10 - viewH);
  if (g.scrollTarget > g.scrollMax) g.scrollTarget = g.scrollMax;
  if (g.scrollY > g.scrollMax) g.scrollY = g.scrollMax;
  auto edgeFade = [&](float top, float bottom, bool down, float amount) {
    if (amount <= 0) return;
    D2D1_GRADIENT_STOP stops[] = {{0, alpha(kBg, down ? amount : 0)}, {1, alpha(kBg, down ? 0 : amount)}};
    ComPtr<ID2D1GradientStopCollection> coll;
    ComPtr<ID2D1LinearGradientBrush> b;
    g.rt->CreateGradientStopCollection(stops, 2, &coll);
    if (coll && SUCCEEDED(g.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, top}, {0, bottom}),
                                                          coll.Get(), &b)))
      g.rt->FillRectangle({view.left, top, view.right, bottom}, b.Get());
  };
  edgeFade(view.top, view.top + 18, true, std::min(1.f, g.scrollY / 18));
  edgeFade(view.bottom - 24, view.bottom, false, std::min(1.f, (g.scrollMax - g.scrollY) / 24));
  if (g.scrollMax > 0) {
    float thumbH = std::max(32.f, viewH * viewH / (viewH + g.scrollMax));
    float ty = view.top + (viewH - thumbH) * (g.scrollY / g.scrollMax);
    fillRound({R1 + 15, ty, R1 + 18, ty + thumbH}, 1.5f, hex(0xFFFFFF, .16f));
  }
  g.rt->PopAxisAlignedClip();
}

void drawSettings(D2D1_COLOR_F accent) {
  const float R0 = g.R0, R1 = g.R1;
  auto creds = auth::credentials();
  bool hasCreds = !creds.clientId.empty() && !creds.clientSecret.empty();


  text(kAppName, {R0, 22, R1 - 140, 48}, g.fTitle.Get(), kText);
  std::wstring sub = g.connected ? (g.displayName.empty() ? L"Signed in to Spotify" : L"Signed in as " + g.displayName)
                     : hasCreds  ? L"Ready to connect"
                                 : L"Add your Spotify app to get started";
  text(sub, {R0, 48, R1 - 130, 68}, g.fSub.Get(), kText2);

  std::wstring pillText = g.connected ? L"Connected" : L"Not connected";
  float pw = measure(pillText, g.fSmall.Get()) + 36;
  D2D1_RECT_F pill = {R1 - pw, 30, R1, 56};
  fillRound(pill, 13, hex(0xFFFFFF, .05f));
  strokeRound(pill, 13, kLine);
  D2D1_COLOR_F dot = g.connected ? kGreen : kAmber;
  fillCircle(pill.left + 15, 43, 6.5f, alpha(dot, .22f));
  fillCircle(pill.left + 15, 43, 3.5f, dot);
  text(pillText, {pill.left + 26, pill.top, pill.right - 6, pill.bottom}, g.fSmall.Get(), kText2);

  drawTabs(accent);

  if (g.tab == kTabAccount) {
    drawAccountTab(accent);
  } else if (g.tab == kTabCustomize) {
    drawCustomizeTab(accent);
  } else {
    float y = kContentY;
    for (size_t i = 0; i < kRows.size(); ++i) {
      if (kRows[i].tab != g.tab) continue;
      drawRow(i, y, 40, accent);
      y += 52;
    }
    if (g.tab == kTabDisplay) drawWebAddress(y + 8);
  }


  float fy = g.H - 34;
  g.brush->SetColor(kLine);
  g.rt->FillRectangle({R0, fy, R1, fy + 1}, g.brush.Get());
  bool ok = g.serverError.empty();
  fillCircle(R0 + 4, fy + 17, 3.5f, ok ? kGreen : kRed);
  std::wstring status = !ok                    ? widen(g.serverError)
                        : wallpaper::running() ? L"Live on your desktop · server on 127.0.0.1:8888"
                                               : L"Wallpaper server running on 127.0.0.1:8888";
  text(status, {R0 + 14, fy + 8, R1, fy + 26}, g.fSmall.Get(), ok ? kText3 : kRed);
}

void draw() {
  float t = seconds(Clock::now() - g.start);
  g.hots.clear();
  g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
  g.rt->Clear(kBg);
  D2D1_COLOR_F accent = currentAccent();
  drawNowPlaying(accent, t);
  drawSettings(accent);
}

void paint() {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(g.hwnd, &ps);
  RECT rc;
  GetClientRect(g.hwnd, &rc);
  if (rc.right > 0 && rc.bottom > 0 && ensureTarget() && SUCCEEDED(g.rt->BindDC(hdc, &rc))) {
    float dpi = 96.f * g.ui;
    g.rt->SetDpi(dpi, dpi);
    g.rt->BeginDraw();
    draw();
    if (g.rt->EndDraw() == D2DERR_RECREATE_TARGET) discardTarget();
  }
  EndPaint(g.hwnd, &ps);
}



void createEditFont() {
  if (g.editFontUi == g.ui && g.editFont) return;
  if (g.editFont) DeleteObject(g.editFont);
  g.editFontUi = g.ui;
  g.editFont = CreateFontW(-(int)std::lround(14 * g.ui), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           g.uiFont.c_str());
  HDC dc = GetDC(g.hwnd);
  HGDIOBJ old = SelectObject(dc, g.editFont);
  TEXTMETRICW tm{};
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(g.hwnd, dc);
  g.editLineH = tm.tmHeight + 2;
  for (HWND e : {g.editId, g.editSecret, g.editYoutube}) {
    SendMessageW(e, WM_SETFONT, (WPARAM)g.editFont, TRUE);
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
  }
}

void placeEdit(HWND e, float fieldY, float rightInset) {
  int x = (int)std::lround((g.R0 + 14) * g.ui);
  int w = (int)std::lround((g.R1 - rightInset) * g.ui) - x;
  int y = (int)std::lround((fieldY + kFieldH / 2) * g.ui - g.editLineH / 2.f);
  SetWindowPos(e, nullptr, x, y, w, g.editLineH, SWP_NOZORDER | SWP_NOACTIVATE);
}

void layout() {
  if (!g.editId) return;
  RECT rc;
  GetClientRect(g.hwnd, &rc);
  float cw = rc.right / g.scale, ch = rc.bottom / g.scale;
  if (cw <= 0 || ch <= 0) return;
  g.zoom = std::clamp(std::min(cw / kBaseW, ch / kBaseH), kMinZoom, kMaxZoom);
  g.ui = g.scale * g.zoom;
  g.W = cw / g.zoom;
  g.H = ch / g.zoom;
  g.leftW = kBaseLeftW + std::max(0.f, g.W - kBaseW) * 0.35f;
  g.R0 = g.leftW + 28;
  g.R1 = g.W - 28;
  createEditFont();
  placeEdit(g.editId, kIdFieldY, 14);
  placeEdit(g.editSecret, kSecretFieldY, 46);
  placeEdit(g.editYoutube, kYoutubeFieldY, 46);
  int show = g.tab == kTabAccount ? SW_SHOW : SW_HIDE;
  for (HWND e : {g.editId, g.editSecret, g.editYoutube}) ShowWindow(e, show);
}

SIZE windowSizeFor(float clientW, float clientH) {
  RECT r{0, 0, (LONG)std::ceil(clientW * g.scale), (LONG)std::ceil(clientH * g.scale)};
  AdjustWindowRectExForDpi(&r, kStyle, FALSE, 0, g.dpi);
  return {r.right - r.left, r.bottom - r.top};
}

void placeInitialWindow() {
  float w = (float)atof(settings::get("windowW").c_str()), h = (float)atof(settings::get("windowH").c_str());
  if (w < kBaseW * kMinZoom || h < kBaseH * kMinZoom) w = kBaseW, h = kBaseH;
  MONITORINFO mi{sizeof mi};
  GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
  RECT wa = mi.rcWork;
  SIZE sz = windowSizeFor(w, h);
  sz.cx = std::min<LONG>(sz.cx, wa.right - wa.left);
  sz.cy = std::min<LONG>(sz.cy, wa.bottom - wa.top);
  SetWindowPos(g.hwnd, nullptr, wa.left + (wa.right - wa.left - sz.cx) / 2, wa.top + (wa.bottom - wa.top - sz.cy) / 2,
               sz.cx, sz.cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

void saveWindowSize() {
  if (IsIconic(g.hwnd) || IsZoomed(g.hwnd)) return;
  RECT rc;
  GetClientRect(g.hwnd, &rc);
  settings::set("windowW", std::to_string((int)std::lround(rc.right / g.scale)));
  settings::set("windowH", std::to_string((int)std::lround(rc.bottom / g.scale)));
}



NOTIFYICONDATAW trayData() {
  NOTIFYICONDATAW nid{sizeof nid};
  nid.hWnd = g.hwnd;
  nid.uID = kTrayId;
  return nid;
}

void addTray() {
  NOTIFYICONDATAW nid = trayData();
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = WM_APP_TRAY;
  nid.hIcon = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                GetSystemMetrics(SM_CYSMICON), 0);
  wcscpy_s(nid.szTip, kAppName);
  Shell_NotifyIconW(NIM_ADD, &nid);
  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void updateTrayTip() {
  NOTIFYICONDATAW nid = trayData();
  nid.uFlags = NIF_TIP | NIF_SHOWTIP;
  std::wstring tip = g.player.hasItem ? widen(g.player.title) + L" — " + widen(g.player.artist) : kAppName;
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void showWindow() {
  ShowWindow(g.hwnd, IsIconic(g.hwnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g.hwnd);
  SetEvent(g.wake);
  invalidate();
}

void hideToTray() {
  ShowWindow(g.hwnd, SW_HIDE);
  updateTimer();
  if (!g.trayHintShown) {
    g.trayHintShown = true;
    NOTIFYICONDATAW nid = trayData();
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, L"Still running in the tray");
    wcscpy_s(nid.szInfo, L"Your wallpaper stays live. Right-click the tray icon to quit.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }
}

void applySetting(const std::string& key);

void trayMenu(int x, int y) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, kMenuOpen, L"Open Spotify Wallpaper");
  AppendMenuW(m, MF_STRING | (wallpaper::running() ? MF_CHECKED : 0), kMenuToggleWallpaper, L"Live desktop wallpaper");
  AppendMenuW(m, MF_STRING, kMenuWallpaper, L"Open wallpaper in browser");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kMenuQuit, L"Quit");
  SetMenuDefaultItem(m, kMenuOpen, FALSE);
  SetForegroundWindow(g.hwnd);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, g.hwnd, nullptr);
  DestroyMenu(m);
  if (cmd == kMenuOpen) showWindow();
  else if (cmd == kMenuWallpaper) openUrl(kWallpaperUrl);
  else if (cmd == kMenuToggleWallpaper) {
    settings::set("desktop", settings::on("desktop") ? "off" : "on");
    applySetting("desktop");
    invalidate();
  } else if (cmd == kMenuQuit) DestroyWindow(g.hwnd);
}



bool saveFields() {
  std::string yt = editText(g.editYoutube);
  if (yt != auth::youtubeKey()) {
    if (auth::saveYoutubeKey(yt)) showToast(yt.empty() ? L"YouTube key removed" : L"YouTube key saved");
    else showToast(L"Couldn't save config.json");
  }
  auto c = auth::credentials();
  if (c.fromEnvironment) return true;
  std::string id = editText(g.editId), secret = editText(g.editSecret);
  if (id == c.clientId && secret == c.clientSecret) return true;
  if (!auth::saveCredentials(id, secret)) {
    showToast(L"Couldn't save config.json");
    return false;
  }
  showToast(L"Saved");
  return true;
}

void runCommand(spotify::Command cmd) {
  HWND hwnd = g.hwnd;
  HANDLE wake = g.wake;
  std::thread([cmd, hwnd, wake] {
    std::string err = spotify::send(cmd);
    if (!err.empty()) PostMessageW(hwnd, WM_APP_TOAST, 1, (LPARAM) new std::wstring(widen(err)));
    Sleep(400);
    SetEvent(wake);
  }).detach();
}

void togglePlay() {
  auto& p = g.player;
  if (!g.connected || !p.hasItem) return;
  p.progressMs = currentProgress();
  g.syncedAt = Clock::now();
  p.playing = !p.playing;
  g.playLockUntil = Clock::now() + std::chrono::milliseconds(1500);
  runCommand(p.playing ? spotify::Command::Play : spotify::Command::Pause);
  invalidate();
}


void updateAudio() {
  bool want = settings::get("visualizer") != "off" && wallpaper::running();
  if (want && !audio::running()) audio::start(g.hwnd, WM_APP_SPECTRUM);
  if (!want && audio::running()) audio::stop();
}


void applySetting(const std::string& key) {
  if (key == "desktop") {
    if (settings::on("desktop")) {
      std::wstring error;
      if (!wallpaper::start(g.inst, kWallpaperUrl, wallpaperOptions(), error)) {
        settings::set("desktop", "off");
        showToast(error);
      }
    } else {
      wallpaper::stop();
    }
  } else if (key == "monitor" || key == "interactive" || key == "pauseCovered") {
    wallpaper::setOptions(wallpaperOptions());
  } else {
    pushLookToWallpaper();
  }
  updateAudio();
}

void setRow(const Row& r, const std::string& value) {
  std::string key = r.key;
  if (key == "startup") {
    bool on = value == "on";
    if (setStartup(on)) g.startup = on;
    else showToast(L"Couldn't change the startup setting");
    return;
  }
  settings::set(key, value);
  applySetting(key);
}

HBITMAP swatchBitmap(COLORREF c) {
  int size = GetSystemMetrics(SM_CXMENUCHECK);
  BITMAPINFO bi{};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) return nullptr;
  auto* px = (uint32_t*)bits;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
      uint32_t rgb = edge ? 0x808080u : ((uint32_t)GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c));
      px[y * size + x] = 0xFF000000u | rgb;
    }
  return bmp;
}

void openColorMenu(const Row& r) {
  std::string current = rowValue(r);
  HMENU m = CreatePopupMenu();
  std::vector<HBITMAP> bitmaps;
  int n = (int)std::size(kColorPresets);
  for (int i = 0; i < n; ++i) {
    std::wstring label = std::wstring(kColorPresets[i].name) + L"\t" + widen(kColorPresets[i].hex);
    AppendMenuW(m, MF_STRING | (current == kColorPresets[i].hex ? MF_CHECKED : 0), i + 1, label.c_str());
    HBITMAP bmp = swatchBitmap(parseHex(kColorPresets[i].hex));
    bitmaps.push_back(bmp);
    MENUITEMINFOW mii{sizeof mii, MIIM_BITMAP};
    mii.hbmpItem = bmp;
    SetMenuItemInfoW(m, i + 1, FALSE, &mii);
  }
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, 100, L"Custom color\u2026");
  POINT pt;
  GetCursorPos(&pt);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g.hwnd, nullptr);
  DestroyMenu(m);
  for (HBITMAP bmp : bitmaps) DeleteObject(bmp);
  if (cmd >= 1 && cmd <= n) {
    setRow(r, kColorPresets[cmd - 1].hex);
  } else if (cmd == 100) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{sizeof cc};
    cc.hwndOwner = g.hwnd;
    cc.rgbResult = parseHex(current);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) setRow(r, toHex(cc.rgbResult));
  }
}

void openChoiceMenu(size_t index) {
  const Row& r = kRows[index];
  if (isColorRow(r)) return openColorMenu(r);
  std::vector<std::wstring> labels;
  std::vector<Choice> choices = std::string(r.key) == "monitor" ? monitorChoices(labels) : r.choices;
  std::string current = rowValue(r);
  HMENU m = CreatePopupMenu();
  for (size_t i = 0; i < choices.size(); ++i) {
    AppendMenuW(m, MF_STRING | (current == choices[i].value ? MF_CHECKED : 0), i + 1, choices[i].label);
    if (std::string(r.key) == "monitor" && i == 1 && choices.size() > 2) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  }
  POINT pt;
  GetCursorPos(&pt);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g.hwnd, nullptr);
  DestroyMenu(m);
  if (cmd > 0 && (size_t)cmd <= choices.size() && current != choices[cmd - 1].value)
    setRow(r, choices[cmd - 1].value);
}

void selectTab(int tab) {
  if (tab == g.tab) return;
  SetFocus(g.hwnd);
  g.tab = tab;
  g.scrollY = g.scrollTarget = 0;
  layout();
}

void onClick(int id) {
  if (id >= kTabHit && id < kTabHit + kTabCount) {
    selectTab(id - kTabHit);
  } else if (id >= kRowHit && id < kRowHit + (int)kRows.size()) {
    size_t index = id - kRowHit;
    const Row& r = kRows[index];
    if (isSwitchRow(r)) setRow(r, rowValue(r) == "on" ? "off" : "on");
    else openChoiceMenu(index);
  }
  switch (id) {
    case kPlay: togglePlay(); break;
    case kPrev: runCommand(spotify::Command::Previous); break;
    case kNext: runCommand(spotify::Command::Next); break;
    case kEye:
      g.secretVisible = !g.secretVisible;
      SendMessageW(g.editSecret, EM_SETPASSWORDCHAR, g.secretVisible ? 0 : 0x25CF, 0);
      InvalidateRect(g.editSecret, nullptr, TRUE);
      break;
    case kCopyRedirect:
    case kCopyUrl:
      copyToClipboard(id == kCopyUrl ? kWallpaperUrl : widen(auth::kRedirectUri));
      g.copiedId = id;
      g.copiedAt = Clock::now();
      showToast(L"Copied to clipboard");
      break;
    case kDashboard: openUrl(L"https://developer.spotify.com/dashboard"); break;
    case kYoutubeLink: openUrl(L"https://console.cloud.google.com/apis/library/youtube.googleapis.com"); break;
    case kEyeYoutube:
      g.youtubeVisible = !g.youtubeVisible;
      SendMessageW(g.editYoutube, EM_SETPASSWORDCHAR, g.youtubeVisible ? 0 : 0x25CF, 0);
      InvalidateRect(g.editYoutube, nullptr, TRUE);
      break;
    case kOpenUrl: openUrl(kWallpaperUrl); break;
    case kConnect:
      if (g.connected) {
        auth::logout();
        showToast(L"Disconnected from Spotify");
      } else if (saveFields()) {
        if (!auth::hasCredentials()) showToast(L"Enter your Client ID and Client Secret first");
        else if (!g.serverError.empty()) showToast(widen(g.serverError));
        else openUrl(std::wstring(kWallpaperUrl) + L"/login");
      }
      break;
    case kReset:
      settings::resetLook();
      pushLookToWallpaper();
      updateAudio();
      showToast(L"Customization reset to defaults");
      break;
  }
  invalidate();
}

int hitTest(LPARAM lp) {
  float x = GET_X_LPARAM(lp) / g.ui, y = GET_Y_LPARAM(lp) / g.ui;
  for (auto it = g.hots.rbegin(); it != g.hots.rend(); ++it)
    if (contains(it->rc, x, y)) return it->id;
  return kNone;
}

void refreshHover() {
  if (!g.trackingMouse || g.pressed) return;
  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(g.hwnd, &pt);
  g.hover = hitTest(MAKELPARAM(pt.x, pt.y));
}



void pollLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::string lastArt;
  bool needName = true;
  while (!g.quitting) {
    DWORD wait = 5000;
    bool visible = IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd);
    if (!auth::isConnected()) {
      needName = true;
      lastArt.clear();
    } else if (visible) {
      if (needName) {
        std::string name = spotify::fetchDisplayName();
        if (!name.empty()) {
          needName = false;
          PostMessageW(g.hwnd, WM_APP_NAME, 0, (LPARAM) new std::wstring(widen(name)));
        }
      }
      auto* p = new spotify::Player(spotify::fetchPlayer());
      wait = p->status == 429 ? std::max(p->retryAfterSec, 5) * 1000 : visible ? 2000 : 5000;
      std::string artUrl = p->ok ? p->artUrl : lastArt;
      PostMessageW(g.hwnd, WM_APP_PLAYER, 0, (LPARAM)p);
      if (artUrl != lastArt) {
        lastArt = artUrl;
        auto* art = new spotify::Art;
        if (!artUrl.empty() && spotify::decodeArt(artUrl, *art)) PostMessageW(g.hwnd, WM_APP_ART, 0, (LPARAM)art);
        else delete art;
      }
    }
    WaitForSingleObject(g.wake, wait);
  }
  CoUninitialize();
}

void setArt(std::shared_ptr<spotify::Art> art) {
  if (art == g.art) return;
  g.prevArt = g.art;
  g.prevArtBmp = g.artBmp;
  g.art = std::move(art);
  g.artBmp.Reset();
  g.artFade = 0;
}

void onPlayer(std::unique_ptr<spotify::Player> p) {
  if (!p->ok || !g.connected) return;
  bool locked = Clock::now() < g.playLockUntil;
  bool playing = locked ? g.player.playing : p->playing;
  std::string oldUrl = g.player.artUrl;
  bool oldHas = g.player.hasItem;
  g.player = *p;
  g.player.playing = playing;
  g.syncedAt = Clock::now();
  if (!g.player.hasItem && oldHas) setArt(nullptr);
  if (g.player.hasItem != oldHas || g.player.artUrl != oldUrl) updateTrayTip();
  invalidate();
}



LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g.taskbarCreatedMsg && msg) {
    addTray();
    wallpaper::restart();
    return 0;
  }
  switch (msg) {
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_TIMER: {
      auto now = Clock::now();
      float dt = std::min(0.1f, seconds(now - g.lastTick));
      g.lastTick = now;
      for (int i = 1; i < kHitMax; ++i) g.anim[i] = approach(g.anim[i], g.hover == i ? 1.f : 0.f, dt * 9);
      for (size_t i = 0; i < kRows.size(); ++i)
        g.rowOn[i] = approach(g.rowOn[i], rowValue(kRows[i]) == "on" ? 1.f : 0.f, dt * 7);
      g.tabAnim += ((float)g.tab - g.tabAnim) * std::min(1.f, dt * 14);
      if (std::fabs(g.tabAnim - (float)g.tab) < 0.002f) g.tabAnim = (float)g.tab;
      if (g.scrollY != g.scrollTarget) {
        g.scrollY += (g.scrollTarget - g.scrollY) * std::min(1.f, dt * 16);
        if (std::fabs(g.scrollY - g.scrollTarget) < 0.3f) g.scrollY = g.scrollTarget;
        refreshHover();
      }
      if (g.artFade < 1) {
        g.artFade = std::min(1.f, g.artFade + dt / 0.8f);
        if (g.artFade >= 1) {
          g.prevArt.reset();
          g.prevArtBmp.Reset();
        }
      }
      invalidate();
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!g.trackingMouse) {
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
        g.trackingMouse = TrackMouseEvent(&tme);
      }
      int h = hitTest(lp);
      if (h != g.hover) {
        g.hover = h;
        SetCursor(LoadCursorW(nullptr, h ? IDC_HAND : IDC_ARROW));
        invalidate();
      }
      return 0;
    }
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      if (g.tab == kTabCustomize && pt.x / g.ui >= g.leftW && g.scrollMax > 0) {
        g.scrollTarget = std::clamp(g.scrollTarget - GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 72.f, 0.f, g.scrollMax);
        invalidate();
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      g.trackingMouse = false;
      if (g.hover) {
        g.hover = kNone;
        invalidate();
      }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT && (HWND)wp == hwnd) {
        SetCursor(LoadCursorW(nullptr, g.hover ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      g.pressed = hitTest(lp);
      if (g.pressed) SetCapture(hwnd);
      invalidate();
      return 0;
    case WM_LBUTTONUP: {
      int id = g.pressed;
      g.pressed = kNone;
      ReleaseCapture();
      if (id && id == hitTest(lp)) onClick(id);
      invalidate();
      return 0;
    }
    case WM_KEYDOWN:
      if (wp == VK_SPACE) togglePlay();
      if (wp == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
        int dir = GetKeyState(VK_SHIFT) < 0 ? kTabCount - 1 : 1;
        selectTab((g.tab + dir) % kTabCount);
        invalidate();
      }
      return 0;

    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if (id == kEditId || id == kEditSecret || id == kEditYoutube) {
        if (code == EN_SETFOCUS) g.focusedEdit = id;
        if (code == EN_KILLFOCUS) {
          if (g.focusedEdit == id) g.focusedEdit = 0;
          saveFields();
        }
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) invalidate();
      } else if (id == IDOK) {
        SetFocus(hwnd);
      }
      return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      SetTextColor((HDC)wp, kTextRef);
      SetBkColor((HDC)wp, kFieldRef);
      return (LRESULT)g.fieldBrush;

    case WM_GETMINMAXINFO: {
      SIZE min = windowSizeFor(kBaseW * kMinZoom, kBaseH * kMinZoom);
      ((MINMAXINFO*)lp)->ptMinTrackSize = {min.cx, min.cy};
      return 0;
    }
    case WM_DPICHANGED: {
      g.dpi = HIWORD(wp);
      g.scale = g.dpi / 96.f;
      const RECT* r = (const RECT*)lp;
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      layout();
      invalidate();
      return 0;
    }
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED) {
        SetEvent(g.wake);
        layout();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      updateTimer();
      return 0;
    case WM_EXITSIZEMOVE: saveWindowSize(); return 0;

    case WM_APP_AUTH: {
      bool was = g.connected;
      g.connected = auth::isConnected();
      if (!g.connected) {
        g.player = {};
        g.displayName.clear();
        setArt(nullptr);
        updateTrayTip();
      } else if (!was) {
        showToast(L"Connected to Spotify");
      }
      SetEvent(g.wake);
      invalidate();
      return 0;
    }
    case WM_APP_PLAYER: onPlayer(std::unique_ptr<spotify::Player>((spotify::Player*)lp)); return 0;
    case WM_APP_ART: {
      std::shared_ptr<spotify::Art> art((spotify::Art*)lp);
      if (g.connected && g.player.hasItem && art->url == g.player.artUrl) setArt(art);
      invalidate();
      return 0;
    }
    case WM_APP_NAME: {
      std::unique_ptr<std::wstring> name((std::wstring*)lp);
      g.displayName = *name;
      invalidate();
      return 0;
    }
    case WM_APP_TOAST: {
      std::unique_ptr<std::wstring> m((std::wstring*)lp);
      if (wp == 1) g.playLockUntil = {};
      showToast(*m);
      return 0;
    }
    case WM_APP_SPECTRUM: {
      std::unique_ptr<std::vector<float>> bands((std::vector<float>*)lp);
      if (!wallpaper::anyVisible()) return 0;
      std::wstring json = L"{\"t\":\"spectrum\",\"b\":[";
      wchar_t buf[16];
      for (size_t i = 0; i < bands->size(); ++i) {
        swprintf_s(buf, L"%s%.3f", i ? L"," : L"", (*bands)[i]);
        json += buf;
      }
      wallpaper::postJson(json + L"]}");
      return 0;
    }
    case WM_APP_SHOW: showWindow(); return 0;
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP: showWindow(); break;
        case WM_CONTEXTMENU: trayMenu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp)); break;
      }
      return 0;

    case WM_CLOSE: hideToTray(); return 0;
    case WM_DISPLAYCHANGE:
      wallpaper::reposition();
      invalidate();
      break;
    case WM_DESTROY: {
      audio::stop();
      wallpaper::stop();
      NOTIFYICONDATAW nid = trayData();
      Shell_NotifyIconW(NIM_DELETE, &nid);
      PostQuitMessage(0);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND makeEdit(int id, const std::string& value, const wchar_t* cue, DWORD extraStyle) {
  HWND e = CreateWindowExW(0, L"EDIT", widen(value).c_str(),
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle, 0, 0, 10, 10, g.hwnd,
                           (HMENU)(INT_PTR)id, g.inst, nullptr);
  SendMessageW(e, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
  SendMessageW(e, EM_SETLIMITTEXT, 256, 0);
  return e;
}

}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\SpotifyWallpaperApp");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kClass, nullptr)) PostMessageW(other, WM_APP_SHOW, 0, 0);
    return 0;
  }
  bool startHidden = cmdLine && wcsstr(cmdLine, L"--tray");

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  enableDarkMenus();

  g.inst = inst;
  g.start = g.lastTick = Clock::now();
  std::wstring dir = exeDir();
  auth::init(dir);
  settings::init(dir);
  youtube::init(dir);
  if (!server::start(dir + L"\\public", g.serverError)) OutputDebugStringA(g.serverError.c_str());

  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2d.GetAddressOf());
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g.dw.GetAddressOf());
  if (!g.d2d || !g.dw) {
    MessageBoxW(nullptr, L"Direct2D isn't available on this system.", kAppName, MB_ICONERROR);
    return 1;
  }
  createFormats();
  g.fieldBrush = CreateSolidBrush(kFieldRef);
  g.startup = startupEnabled();
  g.connected = auth::isConnected();
  g.rowOn.resize(kRows.size());
  for (size_t i = 0; i < kRows.size(); ++i) g.rowOn[i] = rowValue(kRows[i]) == "on" ? 1.f : 0.f;
  g.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
  wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON), 0);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  g.hwnd = CreateWindowExW(0, kClass, kAppName, kStyle, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr,
                           inst, nullptr);
  if (!g.hwnd) return 1;

  BOOL dark = TRUE;
  DwmSetWindowAttribute(g.hwnd, 20 , &dark, sizeof dark);
  COLORREF caption = RGB(0x10, 0x11, 0x16);
  DwmSetWindowAttribute(g.hwnd, 35 , &caption, sizeof caption);

  auto creds = auth::credentials();
  g.editId = makeEdit(kEditId, creds.clientId, L"Paste your app's Client ID", 0);
  g.editSecret = makeEdit(kEditSecret, creds.clientSecret, L"Paste your app's Client Secret", ES_PASSWORD);
  g.editYoutube = makeEdit(kEditYoutube, auth::youtubeKey(), L"Paste a YouTube Data API key", ES_PASSWORD);
  if (creds.fromEnvironment)
    for (HWND e : {g.editId, g.editSecret}) SendMessageW(e, EM_SETREADONLY, TRUE, 0);

  g.dpi = GetDpiForWindow(g.hwnd);
  g.scale = g.dpi / 96.f;
  placeInitialWindow();
  layout();
  auth::setNotifyWindow(g.hwnd, WM_APP_AUTH);
  addTray();
  if (!startHidden) {
    ShowWindow(g.hwnd, SW_SHOW);
    UpdateWindow(g.hwnd);
  }
  if (!g.serverError.empty()) showToast(widen(g.serverError));
  if (settings::on("desktop")) applySetting("desktop");
  updateAudio();
  std::thread(pollLoop).detach();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (IsDialogMessageW(g.hwnd, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g.quitting = true;
  SetEvent(g.wake);
  server::stop();
  ReleaseMutex(instanceMutex);
  CoUninitialize();
  return 0;
}
