#include "spotify.h"

#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#include "json.h"
#include "media.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace spotify {
namespace {

void extractAccent(const uint8_t* px, uint32_t w, uint32_t h, float out[3]) {
  double r = 0, g = 0, b = 0, wt = 0;
  uint32_t step = std::max(1u, std::min(w, h) / 24);
  for (uint32_t y = 0; y < h; y += step)
    for (uint32_t x = 0; x < w; x += step) {
      const uint8_t* p = px + (y * w + x) * 4;
      int B = p[0], G = p[1], R = p[2];
      int mx = std::max({R, G, B}), mn = std::min({R, G, B});
      double weight = 1 + (mx - mn) / 32.0 + (mx > 40 ? 1 : 0);
      r += R * weight; g += G * weight; b += B * weight; wt += weight;
    }
  r /= wt * 255; g /= wt * 255; b /= wt * 255;

  double mx = std::max({r, g, b}), mn = std::min({r, g, b}), hue = 0, sat = 0;
  if (mx != mn) {
    double d = mx - mn, l = (mx + mn) / 2;
    sat = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    hue = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    hue /= 6;
  }
  sat = std::min(sat, 0.55);
  double l = 0.7;
  double q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
  auto f = [&](double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
  };
  out[0] = (float)f(hue + 1.0 / 3);
  out[1] = (float)f(hue);
  out[2] = (float)f(hue - 1.0 / 3);
}

std::string artUrl(const media::State& s) {
  return s.artVersion ? "/art?v=" + std::to_string(s.artVersion) : std::string();
}

// Windows gives no track id, so the same title, artist and album count as the same track.
std::string trackId(const media::State& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s.title + "\n" + s.artist + "\n" + s.album) h = (h ^ c) * 16777619u;
  return std::to_string(h);
}

}

// Everything here comes from the Spotify app on this PC through Windows; nothing calls Spotify's web API.
Player fetchPlayer() {
  Player p;
  media::State s = media::state();
  if (!s.open || s.title.empty()) return p;
  p.hasItem = true;
  p.playing = s.playing;
  p.progressMs = s.positionMs;
  p.durationMs = s.durationMs;
  p.id = trackId(s);
  p.title = s.title;
  p.artist = s.artist;
  p.album = s.album;
  p.artUrl = artUrl(s);
  return p;
}

// Shaped like Spotify's own player response so the wallpaper page reads it the same way.
std::string playerJson() {
  media::State s = media::state();
  if (!s.open || s.title.empty()) return "{\"age\":0,\"player\":null}";
  std::string art = artUrl(s);
  int volume = media::volume();
  return "{\"age\":0,\"player\":{\"is_playing\":" + std::string(s.playing ? "true" : "false") +
         ",\"progress_ms\":" + std::to_string(s.positionMs) + ",\"item\":{\"id\":" + jsonEscape(trackId(s)) +
         ",\"type\":\"track\",\"name\":" + jsonEscape(s.title) + ",\"duration_ms\":" + std::to_string(s.durationMs) +
         ",\"artists\":[{\"name\":" + jsonEscape(s.artist) + "}],\"album\":{\"name\":" + jsonEscape(s.album) +
         ",\"images\":[" + (art.empty() ? "" : "{\"url\":" + jsonEscape(art) + "}") +
         "]}},\"device\":{\"name\":\"this PC\",\"supports_volume\":" + (volume < 0 ? "false" : "true") +
         ",\"volume_percent\":" + (volume < 0 ? "null" : std::to_string(volume)) + "}}}";
}

bool decodeArt(const std::string& url, Art& out) {
  std::string bytes = media::art();
  if (bytes.empty()) return false;

  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
    return false;
  ComPtr<IStream> stream;
  stream.Attach(SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size()));
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> conv;
  if (!stream || FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
      FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom)))
    return false;
  UINT w = 0, h = 0;
  conv->GetSize(&w, &h);
  if (!w || !h || w > 4096 || h > 4096) return false;
  out.pixels.resize((size_t)w * h * 4);
  if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)out.pixels.size(), out.pixels.data()))) return false;
  out.url = url;
  out.width = w;
  out.height = h;
  extractAccent(out.pixels.data(), w, h, out.accent);
  return true;
}

std::string send(Command cmd) {
  static const media::Command map[] = {media::Command::Play, media::Command::Pause, media::Command::Next,
                                       media::Command::Previous};
  return media::send(map[(int)cmd]) ? std::string() : "Open Spotify on this PC to control playback";
}

}
