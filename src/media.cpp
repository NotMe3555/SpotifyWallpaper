#include "media.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
using Microsoft::WRL::ComPtr;

namespace media {
namespace {

using Manager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;
using Status = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

constexpr int64_t kTicksPerMs = 10'000, kSeekTicks = 2000 * kTicksPerMs;

void (*g_onChange)();
std::mutex g_mutex;
Session g_session{nullptr};
event_token g_mediaToken, g_playbackToken, g_timelineToken;
State g_state;
std::string g_art;
int64_t g_position, g_duration, g_updated;

int64_t nowTicks() { return clock::now().time_since_epoch().count(); }

bool isPlaying(const Session& s) {
  auto info = s.GetPlaybackInfo();
  return info && info.PlaybackStatus() == Status::Playing;
}

uint32_t hashBytes(const std::string& bytes) {
  uint32_t h = 2166136261u;
  for (unsigned char c : bytes) h = (h ^ c) * 16777619u;
  return h ? h : 1;
}

void readTimeline(const Session& s, int64_t& position, int64_t& duration, int64_t& updated) {
  auto t = s.GetTimelineProperties();
  position = t ? t.Position().count() : 0;
  duration = t ? (t.EndTime() - t.StartTime()).count() : 0;
  updated = t ? t.LastUpdatedTime().time_since_epoch().count() : nowTicks();
}

void onMedia(const Session& s) try {
  auto props = s.TryGetMediaPropertiesAsync().get();
  if (!props) return;
  std::string art;
  if (auto thumb = props.Thumbnail()) {
    auto stream = thumb.OpenReadAsync().get();
    uint32_t size = (uint32_t)std::min<uint64_t>(stream.Size(), 16u << 20);
    DataReader reader(stream);
    size = reader.LoadAsync(size).get();
    art.resize(size);
    if (size) reader.ReadBytes({(uint8_t*)art.data(), (uint8_t*)art.data() + size});
  }
  int64_t position, duration, updated;
  readTimeline(s, position, duration, updated);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string title = to_string(props.Title());
    if (title != g_state.title) g_position = position, g_duration = duration, g_updated = updated;
    g_state.title = title;
    g_state.artist = to_string(props.Artist());
    g_state.album = to_string(props.AlbumTitle());
    if (!art.empty() || g_state.title.empty()) {
      g_state.artVersion = art.empty() ? 0 : hashBytes(art);
      g_art = std::move(art);
    }
  }
  g_onChange();
} catch (...) {
}

void onPlayback(const Session& s) try {
  bool playing = isPlaying(s), changed;
  int64_t position, duration, updated;
  readTimeline(s, position, duration, updated);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    changed = playing != g_state.playing;
    if (changed) {
      // Freeze or restart the clock at the moment of the change, from whichever reading is newer.
      int64_t now = nowTicks();
      if (updated > g_updated) g_position = position, g_updated = updated;
      if (g_state.playing) g_position += now - g_updated;
      g_updated = now;
      g_state.playing = playing;
    }
  }
  if (changed) g_onChange();
} catch (...) {
}

// Spotify republishes its timeline every few seconds; only a jump away from where playback should be is a seek.
void onTimeline(const Session& s) try {
  int64_t position, duration, updated;
  readTimeline(s, position, duration, updated);
  bool jumped;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    int64_t expected = g_position + (g_state.playing ? updated - g_updated : 0);
    jumped = std::abs(position - expected) > kSeekTicks || duration != g_duration;
    g_position = position;
    g_duration = duration;
    g_updated = updated;
  }
  if (jumped) g_onChange();
} catch (...) {
}

void attach(const Manager& manager) try {
  Session found{nullptr};
  for (auto&& s : manager.GetSessions()) {
    std::wstring app{s.SourceAppUserModelId()};
    std::transform(app.begin(), app.end(), app.begin(), towlower);
    if (app.find(L"spotify") != std::wstring::npos) found = s;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session) {
      g_session.MediaPropertiesChanged(g_mediaToken);
      g_session.PlaybackInfoChanged(g_playbackToken);
      g_session.TimelinePropertiesChanged(g_timelineToken);
    }
    g_session = found;
    g_state = {};
    g_art.clear();
    g_state.open = found != nullptr;
    if (found) {
      g_state.playing = isPlaying(found);
      readTimeline(found, g_position, g_duration, g_updated);
      g_mediaToken = found.MediaPropertiesChanged([](const Session& s, auto&&) { onMedia(s); });
      g_playbackToken = found.PlaybackInfoChanged([](const Session& s, auto&&) { onPlayback(s); });
      g_timelineToken = found.TimelinePropertiesChanged([](const Session& s, auto&&) { onTimeline(s); });
    }
  }
  if (found) onMedia(found);
  else g_onChange();
} catch (...) {
}

Session session() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_session;
}

bool isSpotifyProcess(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return false;
  wchar_t path[MAX_PATH];
  DWORD len = MAX_PATH;
  bool ok = QueryFullProcessImageNameW(process, 0, path, &len) != 0;
  CloseHandle(process);
  if (!ok) return false;
  std::wstring name = path;
  name = name.substr(name.find_last_of(L'\\') + 1);
  return _wcsicmp(name.c_str(), L"Spotify.exe") == 0;
}

// Spotify's own volume isn't exposed without its web API, so this is its slider in the Windows volume mixer.
template <class F>
bool eachSpotifyVolume(F&& each) {
  bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), found = false;
  {
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<IAudioSessionEnumerator> sessions;
    int count = 0;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
        SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)manager.GetAddressOf())) &&
        SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && SUCCEEDED(sessions->GetCount(&count))) {
      for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        ComPtr<IAudioSessionControl2> control2;
        ComPtr<ISimpleAudioVolume> volume;
        DWORD pid = 0;
        if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2)) ||
            FAILED(control2->GetProcessId(&pid)) || !isSpotifyProcess(pid) || FAILED(control.As(&volume)))
          continue;
        each(volume.Get());
        found = true;
      }
    }
  }
  if (initialized) CoUninitialize();
  return found;
}

}

void start(void (*onChange)()) {
  g_onChange = onChange;
  std::thread([] {
    try {
      init_apartment(apartment_type::multi_threaded);
      Manager manager = Manager::RequestAsync().get();
      manager.SessionsChanged([](const Manager& m, auto&&) { attach(m); });
      attach(manager);
      Sleep(INFINITE);
    } catch (...) {
    }
  }).detach();
}

State state() {
  std::lock_guard<std::mutex> lock(g_mutex);
  State s = g_state;
  int64_t position = g_position + (s.playing ? nowTicks() - g_updated : 0);
  s.durationMs = g_duration / kTicksPerMs;
  s.positionMs = std::clamp<int64_t>(position / kTicksPerMs, 0, std::max<int64_t>(s.durationMs, 0));
  return s;
}

std::string art() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_art;
}

bool send(Command cmd) try {
  Session s = session();
  if (!s) return false;
  switch (cmd) {
    case Command::Play: return s.TryPlayAsync().get();
    case Command::Pause: return s.TryPauseAsync().get();
    case Command::Next: return s.TrySkipNextAsync().get();
    case Command::Previous: return s.TrySkipPreviousAsync().get();
  }
  return false;
} catch (...) {
  return false;
}

bool seek(int64_t positionMs) try {
  Session s = session();
  return s && s.TryChangePlaybackPositionAsync(positionMs * kTicksPerMs).get();
} catch (...) {
  return false;
}

int volume() {
  float level = -1;
  eachSpotifyVolume([&](ISimpleAudioVolume* v) {
    float f = 0;
    if (level < 0 && SUCCEEDED(v->GetMasterVolume(&f))) level = f;
  });
  return level < 0 ? -1 : (int)(level * 100 + 0.5f);
}

bool setVolume(int percent) {
  float level = std::clamp(percent, 0, 100) / 100.f;
  return eachSpotifyVolume([&](ISimpleAudioVolume* v) { v->SetMasterVolume(level, nullptr); });
}

}
