#include "wallpaper.h"

#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <WebView2.h>

#include <memory>

#include "util.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace wallpaper {
namespace {

constexpr wchar_t kHostClass[] = L"SpotifyWallpaperHost";


struct Surface {
  std::wstring device;
  RECT screen{};
  HWND host = nullptr;
  HWND widget = nullptr;
  bool paused = false;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> view;
};

HINSTANCE g_inst;
std::wstring g_url, g_dataDir;
Options g_options;
bool g_running;
std::vector<std::unique_ptr<Surface>> g_surfaces;
HWND g_insertAfter;
HWND g_parent;
HHOOK g_hook;
UINT_PTR g_coverTimer;
Surface* g_dragging;
ComPtr<ICoreWebView2Environment> g_env;
bool g_envPending;

void log(const std::string& line) {
  static bool fresh = true;
  std::wstring file = g_dataDir + L"\\wallpaper.log";
  std::string text;
  if (!fresh) readFile(file, text);
  fresh = false;
  if (text.size() > 64 * 1024) text.clear();
  writeFile(file, text + line + "\r\n");
}

std::wstring className(HWND h) {
  wchar_t buf[128] = {};
  GetClassNameW(h, buf, (int)std::size(buf));
  return buf;
}

std::string rectStr(const RECT& r) {
  return std::to_string(r.left) + "," + std::to_string(r.top) + " " + std::to_string(r.right - r.left) + "x" +
         std::to_string(r.bottom - r.top);
}

void resizeWebView(Surface& s) {
  if (!s.controller || !s.host) return;
  RECT rc;
  GetClientRect(s.host, &rc);
  s.controller->put_Bounds(rc);
}

void place(Surface& s) {
  RECT r = s.screen;
  MapWindowPoints(HWND_DESKTOP, g_parent, (POINT*)&r, 2);
  SetWindowPos(s.host, g_insertAfter ? g_insertAfter : HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  resizeWebView(s);
}


bool findDesktop() {
  HWND progman = FindWindowW(L"Progman", nullptr);
  if (!progman) return false;

  DWORD_PTR result;
  SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
  SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

  HWND defView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
  HWND worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
  if (defView && worker) {
    g_parent = progman;
    g_insertAfter = defView;
    log("desktop: 24H2 layout");
    return true;
  }

  HWND target = nullptr;
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
          *(HWND*)lp = FindWindowExW(nullptr, h, L"WorkerW", nullptr);
          return FALSE;
        }
        return TRUE;
      },
      (LPARAM)&target);
  g_parent = target;
  g_insertAfter = nullptr;
  log(target ? "desktop: classic layout" : "couldn't find the desktop WorkerW");
  return target != nullptr;
}

HWND findWidget(HWND host) {
  HWND found = nullptr;
  EnumChildWindows(
      host,
      [](HWND h, LPARAM lp) -> BOOL {
        if (className(h) == L"Chrome_RenderWidgetHostHWND") {
          *(HWND*)lp = h;
          return FALSE;
        }
        return TRUE;
      },
      (LPARAM)&found);
  return found;
}

Surface* surfaceAt(POINT pt) {
  for (auto& s : g_surfaces)
    if (PtInRect(&s->screen, pt)) return s.get();
  return nullptr;
}

bool isOurs(HWND h) {
  for (auto& s : g_surfaces)
    if (h == s->host || IsChild(s->host, h)) return true;
  return false;
}


bool overDesktop(POINT pt) {
  HWND h = WindowFromPoint(pt);
  if (!h) return false;
  std::wstring c = className(h);
  if (c == L"SysListView32") {
    HWND p = GetParent(h);
    return p && className(p) == L"SHELLDLL_DefView";
  }
  return c == L"SHELLDLL_DefView" || c == L"WorkerW" || c == L"Progman" || isOurs(h);
}


LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && !g_surfaces.empty()) {
    auto* m = (MSLLHOOKSTRUCT*)lp;
    Surface* s = g_dragging ? g_dragging : overDesktop(m->pt) ? surfaceAt(m->pt) : nullptr;
    if (s && !s->paused) {
      if (!s->widget || !IsWindow(s->widget)) s->widget = findWidget(s->host);
      if (HWND w = s->widget) {
        POINT p = m->pt;
        ScreenToClient(w, &p);
        LPARAM pos = MAKELPARAM(p.x, p.y);
        switch (wp) {
          case WM_MOUSEMOVE: PostMessageW(w, WM_MOUSEMOVE, g_dragging ? MK_LBUTTON : 0, pos); break;
          case WM_LBUTTONDOWN:
            g_dragging = s;
            PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, pos);
            break;
          case WM_LBUTTONUP: PostMessageW(w, WM_LBUTTONUP, 0, pos); break;
          case WM_MOUSEWHEEL:
            PostMessageW(w, WM_MOUSEWHEEL, MAKEWPARAM(0, HIWORD(m->mouseData)), MAKELPARAM(m->pt.x, m->pt.y));
            break;
        }
      }
    }
    if (wp == WM_LBUTTONUP) g_dragging = nullptr;
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

void updateHook() {
  bool want = g_running && g_options.interactive;
  if (want && !g_hook) g_hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, g_inst, 0);
  if (!want && g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_dragging = nullptr;
  }
}


void CALLBACK coverCheck(HWND, UINT, UINT_PTR, DWORD) {
  HWND fg = GetForegroundWindow();
  RECT fr{};
  bool candidate = g_options.pauseCovered && fg && IsWindowVisible(fg) && !isOurs(fg) && GetWindowRect(fg, &fr);
  if (candidate) {
    std::wstring c = className(fg);
    candidate = c != L"WorkerW" && c != L"Progman" && c != L"Shell_TrayWnd" && c != L"Shell_SecondaryTrayWnd";
  }
  for (auto& s : g_surfaces) {
    bool covered = false;
    if (candidate) {
      RECT overlap;
      IntersectRect(&overlap, &fr, &s->screen);
      bool fills = fr.left <= s->screen.left && fr.top <= s->screen.top && fr.right >= s->screen.right &&
                   fr.bottom >= s->screen.bottom;
      covered = fills || (IsZoomed(fg) && !IsRectEmpty(&overlap) &&
                          MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) ==
                              MonitorFromRect(&s->screen, MONITOR_DEFAULTTONULL));
    }
    if (covered != s->paused) {
      s->paused = covered;
      if (s->controller) s->controller->put_IsVisible(covered ? FALSE : TRUE);
    }
  }
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      for (auto& s : g_surfaces)
        if (s->host == hwnd) resizeWebView(*s);
      return 0;
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      HBRUSH b = CreateSolidBrush(RGB(7, 8, 11));
      FillRect((HDC)wp, &rc, b);
      DeleteObject(b);
      return 1;
    }
    case WM_DESTROY:
      for (auto& s : g_surfaces)
        if (s->host == hwnd) s->host = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool isOurPage(const std::wstring& uri) {
  return uri == g_url || uri.rfind(g_url + L"/", 0) == 0;
}

void configureView(Surface& s) {
  ComPtr<ICoreWebView2Settings> st;
  if (SUCCEEDED(s.view->get_Settings(&st))) {
    st->put_AreDefaultContextMenusEnabled(FALSE);
    st->put_AreDevToolsEnabled(FALSE);
    st->put_IsStatusBarEnabled(FALSE);
    st->put_IsZoomControlEnabled(FALSE);
    st->put_AreDefaultScriptDialogsEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> st3;
    if (SUCCEEDED(st.As(&st3))) st3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
  }
  ComPtr<ICoreWebView2Controller2> c2;
  if (SUCCEEDED(s.controller.As(&c2))) c2->put_DefaultBackgroundColor({255, 7, 8, 11});

  EventRegistrationToken token;

  s.view->add_NavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            LPWSTR raw = nullptr;
            args->get_Uri(&raw);
            std::wstring uri = raw ? raw : L"";
            CoTaskMemFree(raw);
            if (!isOurPage(uri)) {
              args->put_Cancel(TRUE);
              ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return S_OK;
          })
          .Get(),
      &token);
  s.view->add_NewWindowRequested(
      Callback<ICoreWebView2NewWindowRequestedEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            LPWSTR raw = nullptr;
            args->get_Uri(&raw);
            if (raw) ShellExecuteW(nullptr, L"open", raw, nullptr, nullptr, SW_SHOWNORMAL);
            CoTaskMemFree(raw);
            args->put_Handled(TRUE);
            return S_OK;
          })
          .Get(),
      &token);
  s.view->add_ProcessFailed(
      Callback<ICoreWebView2ProcessFailedEventHandler>(
          [&s](ICoreWebView2* view, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
            log("webview process failed, reloading");
            s.widget = nullptr;
            view->Reload();
            return S_OK;
          })
          .Get(),
      &token);
}

void createController(Surface* s) {
  HWND host = s->host;
  g_env->CreateCoreWebView2Controller(
      host, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [host](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                  Surface* s = nullptr;
                  for (auto& x : g_surfaces)
                    if (x->host == host) s = x.get();
                  if (!s || FAILED(hr) || !controller) {
                    if (controller) controller->Close();
                    if (FAILED(hr)) log("webview controller failed: " + std::to_string(hr));
                    return S_OK;
                  }
                  s->controller = controller;
                  s->controller->get_CoreWebView2(&s->view);
                  configureView(*s);
                  resizeWebView(*s);
                  if (s->paused) s->controller->put_IsVisible(FALSE);
                  s->view->Navigate(g_url.c_str());
                  log("webview ready on " + narrow(s->device));
                  return S_OK;
                })
                .Get());
}

void createWebViews() {
  if (g_env) {
    for (auto& s : g_surfaces)
      if (!s->controller && s->host) createController(s.get());
    return;
  }
  if (g_envPending) return;
  g_envPending = true;
  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, (g_dataDir + L"\\WebView2").c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            g_envPending = false;
            if (FAILED(hr) || !env) {
              log("webview environment failed: " + std::to_string(hr));
              return S_OK;
            }
            g_env = env;
            createWebViews();
            return S_OK;
          })
          .Get());
  if (FAILED(hr)) {
    g_envPending = false;
    log("CreateCoreWebView2EnvironmentWithOptions failed: " + std::to_string(hr));
  }
}

struct MonitorEntry {
  std::wstring device;
  RECT rect;
  bool primary;
};

std::vector<MonitorEntry> enumMonitors() {
  std::vector<MonitorEntry> list;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(m, &mi))
          ((std::vector<MonitorEntry>*)lp)->push_back({mi.szDevice, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
      },
      (LPARAM)&list);
  return list;
}

bool wanted(const MonitorEntry& m, const std::vector<MonitorEntry>& all) {
  if (g_options.monitor == "all") return true;
  if (g_options.monitor != "primary") {
    bool exists = false;
    for (auto& x : all) exists |= narrow(x.device) == g_options.monitor;
    if (exists) return narrow(m.device) == g_options.monitor;
  }
  return m.primary;
}

bool createSurfaces(std::wstring& error) {
  if (!findDesktop()) {
    error = L"Couldn't find the desktop to draw on";
    return false;
  }
  auto all = enumMonitors();
  for (auto& m : all) {
    if (!wanted(m, all)) continue;
    auto s = std::make_unique<Surface>();
    s->device = m.device;
    s->screen = m.rect;
    s->host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"Spotify Wallpaper", WS_POPUP, 0, 0,
                              100, 100, nullptr, nullptr, g_inst, nullptr);
    if (!s->host) continue;
    SetWindowLongPtrW(s->host, GWL_STYLE, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    if (g_insertAfter) {
      SetWindowLongPtrW(s->host, GWL_EXSTYLE, GetWindowLongPtrW(s->host, GWL_EXSTYLE) | WS_EX_LAYERED);
      SetLayeredWindowAttributes(s->host, 0, 255, LWA_ALPHA);
    }
    SetParent(s->host, g_parent);
    place(*s);
    log("surface on " + narrow(m.device) + " at " + rectStr(m.rect));
    g_surfaces.push_back(std::move(s));
  }
  if (g_surfaces.empty()) {
    error = L"Couldn't create the wallpaper window";
    return false;
  }
  createWebViews();
  return true;
}

void destroySurfaces() {
  g_dragging = nullptr;
  for (auto& s : g_surfaces) {
    if (s->controller) s->controller->Close();
    HWND parent = s->host ? GetParent(s->host) : nullptr;
    if (s->host) DestroyWindow(s->host);

    if (parent) RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }
  g_surfaces.clear();
}

}

std::vector<Monitor> monitors() {
  std::vector<Monitor> out;
  for (auto& m : enumMonitors()) {
    std::wstring num = m.device;
    size_t digits = num.find_last_not_of(L"0123456789");
    num = digits == std::wstring::npos ? num : num.substr(digits + 1);
    std::wstring label = L"Display " + num + L" · " + std::to_wstring(m.rect.right - m.rect.left) + L"×" +
                         std::to_wstring(m.rect.bottom - m.rect.top);
    if (m.primary) label += L" (main)";
    out.push_back({narrow(m.device), label, m.primary});
  }
  return out;
}

bool start(HINSTANCE inst, const std::wstring& url, const Options& options, std::wstring& error) {
  if (g_running) return true;
  g_inst = inst;
  g_url = url;
  g_options = options;
  if (g_dataDir.empty()) {
    PWSTR local = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    g_dataDir = std::wstring(local ? local : L".") + L"\\SpotifyWallpaper";
    CoTaskMemFree(local);
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
  }

  LPWSTR version = nullptr;
  if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) || !version) {
    error = L"The live wallpaper needs the Microsoft Edge WebView2 Runtime";
    return false;
  }
  CoTaskMemFree(version);

  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = hostProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kHostClass;
    registered = RegisterClassExW(&wc) != 0;
  }
  if (!createSurfaces(error)) {
    destroySurfaces();
    return false;
  }
  g_running = true;
  updateHook();
  if (!g_coverTimer) g_coverTimer = SetTimer(nullptr, 0, 1500, coverCheck);
  return true;
}

void stop() {
  g_running = false;
  updateHook();
  if (g_coverTimer) KillTimer(nullptr, g_coverTimer);
  g_coverTimer = 0;
  destroySurfaces();
}

bool running() { return g_running; }

void setOptions(const Options& options) {
  bool monitorChanged = options.monitor != g_options.monitor;
  g_options = options;
  if (!g_running) return;
  updateHook();
  if (!options.pauseCovered)
    for (auto& s : g_surfaces)
      if (s->paused) {
        s->paused = false;
        if (s->controller) s->controller->put_IsVisible(TRUE);
      }
  if (monitorChanged) restart();
}

void restart() {
  if (!g_running) return;
  destroySurfaces();
  std::wstring error;
  if (!createSurfaces(error)) log("restart failed");
}

void reposition() {
  if (!g_running) return;
  restart();
}

void runScript(const std::wstring& js) {
  for (auto& s : g_surfaces)
    if (s->view) s->view->ExecuteScript(js.c_str(), nullptr);
}

void postJson(const std::wstring& json) {
  for (auto& s : g_surfaces)
    if (s->view && !s->paused) s->view->PostWebMessageAsJson(json.c_str());
}

bool anyVisible() {
  for (auto& s : g_surfaces)
    if (!s->paused) return true;
  return false;
}

}
