#include "audio.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace audio {
namespace {

constexpr int kFftSize = 2048;
constexpr double kPi = 3.14159265358979323846;

std::atomic<bool> g_run{false};
std::thread g_thread;

void fft(std::vector<std::complex<float>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = -2 * kPi / (double)len;
    std::complex<float> wl((float)cos(ang), (float)sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<float> w(1);
      for (size_t k = 0; k < len / 2; ++k) {
        auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

std::wstring defaultDeviceId(IMMDeviceEnumerator* en) {
  ComPtr<IMMDevice> dev;
  LPWSTR id = nullptr;
  if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || FAILED(dev->GetId(&id))) return {};
  std::wstring out = id;
  CoTaskMemFree(id);
  return out;
}


void captureSession(IMMDeviceEnumerator* en, HWND hwnd, UINT msg) {
  ComPtr<IMMDevice> dev;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioCaptureClient> capture;
  WAVEFORMATEX* fmt = nullptr;
  if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
      FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf())) ||
      FAILED(client->GetMixFormat(&fmt))) {
    Sleep(1000);
    return;
  }
  std::wstring deviceId = defaultDeviceId(en);
  bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                 (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                  ((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  int channels = fmt->nChannels, bits = fmt->wBitsPerSample;
  double rate = fmt->nSamplesPerSec;
  bool ok = SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0, fmt,
                                         nullptr)) &&
            SUCCEEDED(client->GetService(IID_PPV_ARGS(&capture))) && SUCCEEDED(client->Start());
  CoTaskMemFree(fmt);
  if (!ok || (!isFloat && bits != 16)) {
    Sleep(1000);
    return;
  }


  int edges[kBands + 1];
  for (int b = 0; b <= kBands; ++b) {
    double hz = 40 * pow(16000.0 / 40, (double)b / kBands);
    edges[b] = std::max(1, (int)(hz / rate * kFftSize));
  }
  std::vector<float> window(kFftSize), ring(kFftSize), smooth(kBands);
  for (int i = 0; i < kFftSize; ++i) window[i] = 0.5f - 0.5f * (float)cos(2 * kPi * i / (kFftSize - 1));
  size_t head = 0;
  ULONGLONG lastSend = 0, lastData = GetTickCount64(), lastDeviceCheck = GetTickCount64();
  std::vector<std::complex<float>> buf(kFftSize);

  while (g_run) {
    Sleep(8);
    UINT32 packet = 0;
    if (FAILED(capture->GetNextPacketSize(&packet))) break;
    while (packet) {
      BYTE* data;
      UINT32 frames;
      DWORD flags;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
      for (UINT32 f = 0; f < frames; ++f) {
        float sum = 0;
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
          for (int c = 0; c < channels; ++c)
            sum += isFloat ? ((float*)data)[f * channels + c] : ((int16_t*)data)[f * channels + c] / 32768.f;
        ring[head] = sum / channels;
        head = (head + 1) % kFftSize;
      }
      capture->ReleaseBuffer(frames);
      lastData = GetTickCount64();
      if (FAILED(capture->GetNextPacketSize(&packet))) break;
    }
    ULONGLONG now = GetTickCount64();
    if (now - lastData > 120) {
      std::fill(ring.begin(), ring.end(), 0.f);
    }
    if (now - lastDeviceCheck > 2000) {
      lastDeviceCheck = now;
      if (defaultDeviceId(en) != deviceId) break;
    }
    if (now - lastSend < 12) continue;
    lastSend = now;

    for (int i = 0; i < kFftSize; ++i) buf[i] = {ring[(head + i) % kFftSize] * window[i], 0};
    fft(buf);
    auto* bands = new std::vector<float>(kBands);
    for (int b = 0; b < kBands; ++b) {
      float peak = 0;
      for (int k = edges[b]; k <= std::max(edges[b], edges[b + 1] - 1) && k < kFftSize / 2; ++k)
        peak = std::max(peak, std::abs(buf[k]));
      float mag = peak * 4.f / kFftSize;
      float db = 20 * log10f(mag + 1e-9f);
      float tilt = 6.f * (float)b / kBands;
      float v = std::clamp((db + tilt + 62.f) / 54.f, 0.f, 1.f);
      float& s = smooth[b];
      s = v > s ? s + (v - s) * 0.6f : s + (v - s) * 0.18f;
      (*bands)[b] = s;
    }
    if (!PostMessageW(hwnd, msg, 0, (LPARAM)bands)) delete bands;
  }
  client->Stop();
}

void loop(HWND hwnd, UINT msg) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    ComPtr<IMMDeviceEnumerator> en;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
      while (g_run) captureSession(en.Get(), hwnd, msg);
  }
  CoUninitialize();
}

}

void start(HWND hwnd, UINT msg) {
  if (g_run) return;
  if (g_thread.joinable()) g_thread.join();
  g_run = true;
  g_thread = std::thread(loop, hwnd, msg);
}

void stop() {
  g_run = false;
  if (g_thread.joinable()) g_thread.join();
}

bool running() { return g_run; }

}
