#include "spotify_capture.h"

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings.h"

using Microsoft::WRL::ComPtr;

namespace spotifycap {
namespace {

constexpr float kQuiet = 0.001f;
constexpr size_t kMaxQueued = 16;

std::atomic<bool> g_run{false};
std::atomic<bool> g_muteWanted{false};
std::atomic<float> g_gain{1.f};
std::thread g_captureThread, g_sendThread;
Sink g_sink;




std::mutex g_queueMutex;
std::condition_variable g_queueCv;
std::deque<std::vector<int16_t>> g_queue;


std::atomic<double> g_inSq{0}, g_outSq{0};
std::atomic<uint64_t> g_inN{0}, g_outN{0}, g_packets{0}, g_dropped{0};
std::atomic<DWORD> g_pid{0};

class ActivateHandler
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
 public:
  HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ~ActivateHandler() { CloseHandle(done); }
  HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
    SetEvent(done);
    return S_OK;
  }
};


DWORD spotifyPid() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  std::vector<std::pair<DWORD, DWORD>> procs;
  PROCESSENTRY32W pe{sizeof pe};
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    if (_wcsicmp(pe.szExeFile, L"Spotify.exe") == 0) procs.push_back({pe.th32ProcessID, pe.th32ParentProcessID});
  CloseHandle(snap);
  for (auto& [pid, parent] : procs)
    if (std::none_of(procs.begin(), procs.end(), [&](auto& p) { return p.first == parent; })) return pid;
  return 0;
}


std::vector<ComPtr<ISimpleAudioVolume>> spotifySliders() {
  std::vector<ComPtr<ISimpleAudioVolume>> out;
  ComPtr<IMMDeviceEnumerator> en;
  ComPtr<IMMDeviceCollection> devices;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
      FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
    return out;
  UINT count = 0;
  devices->GetCount(&count);
  for (UINT d = 0; d < count; ++d) {
    ComPtr<IMMDevice> dev;
    ComPtr<IAudioSessionManager2> mgr;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(devices->Item(d, &dev)) ||
        FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf())) ||
        FAILED(mgr->GetSessionEnumerator(&sessions)))
      continue;
    int n = 0;
    sessions->GetCount(&n);
    for (int i = 0; i < n; ++i) {
      ComPtr<IAudioSessionControl> c;
      ComPtr<IAudioSessionControl2> c2;
      DWORD pid = 0;
      if (FAILED(sessions->GetSession(i, &c)) || FAILED(c.As(&c2)) || FAILED(c2->GetProcessId(&pid)) || !pid) continue;
      wchar_t path[MAX_PATH] = {};
      DWORD len = MAX_PATH;
      if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        QueryFullProcessImageNameW(h, 0, path, &len);
        CloseHandle(h);
      }
      const wchar_t* name = wcsrchr(path, L'\\');
      ComPtr<ISimpleAudioVolume> v;
      if (name && _wcsicmp(name + 1, L"Spotify.exe") == 0 && SUCCEEDED(c.As(&v))) out.push_back(v);
    }
  }
  return out;
}

void setSliders(float level) {
  for (auto& v : spotifySliders()) v->SetMasterVolume(level, nullptr);
}



float updateMute(bool want, float& original, bool& muted, bool* changed = nullptr) {
  if (want) {
    auto sliders = spotifySliders();
    for (auto& v : sliders) {
      float level = 1;
      v->GetMasterVolume(&level);
      if (level > kQuiet * 1.5f) original = level;
      if (level != kQuiet) {
        v->SetMasterVolume(kQuiet, nullptr);
        if (changed) *changed = true;
      }
    }
    if (!sliders.empty()) {
      muted = true;

      settings::set("mixerRestore", std::to_string(original));
    }
    return muted ? original / kQuiet : 1.f;
  }
  if (muted) {
    setSliders(original);
    settings::set("mixerRestore", "");
    muted = false;
  }
  return 1.f;
}

void enqueue(std::vector<int16_t>&& chunk) {
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_queue.size() >= kMaxQueued) {
      g_queue.pop_front();
      ++g_dropped;
    }
    g_queue.push_back(std::move(chunk));
  }
  g_queueCv.notify_one();
}

void sendLoop() {
  for (;;) {
    std::vector<int16_t> chunk;
    {
      std::unique_lock<std::mutex> lock(g_queueMutex);
      g_queueCv.wait(lock, [] { return !g_queue.empty() || !g_run; });
      if (!g_run) return;
      chunk = std::move(g_queue.front());
      g_queue.pop_front();
    }
    double sq = 0;
    for (size_t i = 0; i < chunk.size(); i += 4) sq += (chunk[i] / 32768.0) * (chunk[i] / 32768.0);
    g_outSq = g_outSq + sq;
    g_outN += chunk.size() / 4;
    if (g_sink) g_sink(chunk.data(), chunk.size() / 2);
  }
}


void captureSession(DWORD pid, float& original, bool& muted) {
  g_pid = pid;
  AUDIOCLIENT_ACTIVATION_PARAMS params{};
  params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  params.ProcessLoopbackParams.TargetProcessId = pid;
  params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
  PROPVARIANT pv{};
  pv.vt = VT_BLOB;
  pv.blob.cbSize = sizeof params;
  pv.blob.pBlobData = (BYTE*)&params;

  auto handler = Microsoft::WRL::Make<ActivateHandler>();
  ComPtr<IActivateAudioInterfaceAsyncOperation> op;
  if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                         handler.Get(), &op)) ||
      WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0)
    return;
  HRESULT hr = E_FAIL;
  ComPtr<IUnknown> unk;
  ComPtr<IAudioClient> client;
  if (FAILED(op->GetActivateResult(&hr, &unk)) || FAILED(hr) || FAILED(unk.As(&client))) return;


  WAVEFORMATEX fmt{WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 48000 * 8, 8, 32, 0};
  HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ComPtr<IAudioCaptureClient> capture;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                200000, 0, &fmt, nullptr)) ||
      FAILED(client->SetEventHandle(ready)) || FAILED(client->GetService(IID_PPV_ARGS(&capture))) ||
      FAILED(client->Start())) {
    CloseHandle(ready);
    return;
  }

  float gain = 1;
  ULONGLONG lastMuteCheck = 0, settleUntil = 0;
  std::vector<int16_t> chunk;
  chunk.reserve(kChunkFrames * 2);
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);

  while (g_run) {
    WaitForSingleObject(ready, 100);
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) break;
    ULONGLONG now = GetTickCount64();
    if (now - lastMuteCheck > 1500) {
      lastMuteCheck = now;
      bool changed = false;
      gain = updateMute(g_muteWanted, original, muted, &changed);
      g_gain = gain;
      if (changed) settleUntil = now + 300;
    }
    UINT32 packet = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
      BYTE* data;
      UINT32 frames;
      DWORD flags;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
      ++g_packets;
      const float* f = (const float*)data;
      bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || GetTickCount64() < settleUntil;
      double sq = 0;
      for (UINT32 i = 0; i < frames * 2; ++i) {
        float in = silent ? 0.f : f[i];
        if ((i & 3) == 0) sq += (double)in * in;
        float s = std::clamp(in * gain, -1.f, 1.f);
        chunk.push_back((int16_t)std::lround(s * 32767.f));
        if (chunk.size() == kChunkFrames * 2) {
          enqueue(std::move(chunk));
          chunk = std::vector<int16_t>();
          chunk.reserve(kChunkFrames * 2);
        }
      }
      g_inSq = g_inSq + sq;
      g_inN += frames / 2;
      capture->ReleaseBuffer(frames);
    }
  }
  client->Stop();
  if (process) CloseHandle(process);
  CloseHandle(ready);
}

void captureLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  float original = 1;
  bool muted = false;
  std::string saved = settings::get("mixerRestore");
  if (!saved.empty() && atof(saved.c_str()) > 0) original = (float)atof(saved.c_str());
  while (g_run) {
    DWORD pid = spotifyPid();
    if (pid) captureSession(pid, original, muted);
    for (int i = 0; i < 20 && g_run; ++i) Sleep(100);
  }
  updateMute(false, original, muted);
  CoUninitialize();
}

std::string dbfs(double sq, uint64_t n) {
  double rms = n ? std::sqrt(sq / n) : 0;
  char buf[16];
  snprintf(buf, sizeof buf, "%.1f", rms > 0 ? 20 * std::log10(rms) : -120.0);
  return buf;
}

}

void recoverMixer() {
  std::string saved = settings::get("mixerRestore");
  if (saved.empty()) return;
  float level = (float)atof(saved.c_str());
  if (level > 0 && level <= 1) setSliders(level);
  settings::set("mixerRestore", "");
}

void start(Sink sink) {
  if (g_run) return;
  stop();
  g_sink = std::move(sink);
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_queue.clear();
  }
  g_run = true;
  g_captureThread = std::thread(captureLoop);
  g_sendThread = std::thread(sendLoop);
}

void stop() {
  g_run = false;
  g_queueCv.notify_all();
  if (g_captureThread.joinable()) g_captureThread.join();
  if (g_sendThread.joinable()) g_sendThread.join();
}

bool running() { return g_run; }

void setMuteLocal(bool mute) { g_muteWanted = mute; }

float currentGain() { return g_gain; }

std::string takeStats() {
  std::string out = "capture: pid=" + std::to_string(g_pid.load()) + " packets=" + std::to_string(g_packets.load()) +
                    " in=" + dbfs(g_inSq, g_inN) + " dBFS queueDrops=" + std::to_string(g_dropped.load());
  g_inSq = 0;
  g_outSq = 0;
  g_inN = 0;
  g_outN = 0;
  g_packets = 0;
  g_dropped = 0;
  return out;
}

}
