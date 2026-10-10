#include "spotify.h"

#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#include "auth.h"
#include "http_client.h"
#include "json.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace spotify {
namespace {

HttpResponse api(const wchar_t* method, const std::wstring& path) {
  std::string token = auth::accessToken();
  if (token.empty()) {
    HttpResponse r;
    r.status = 401;
    return r;
  }
  std::vector<std::wstring> headers{L"Authorization: Bearer " + widen(token)};
  if (wcscmp(method, L"GET") != 0) headers.push_back(L"Content-Length: 0");
  return httpRequest(method, L"https://api.spotify.com/v1" + path, headers);
}

std::string explain(const HttpResponse& r) {
  std::string reason = Json::parse(r.body)["error"]["reason"].str();
  if (r.status == 404 || reason == "NO_ACTIVE_DEVICE") return "Open Spotify on a device to start listening";
  if (reason == "PREMIUM_REQUIRED") return "Playback controls need Spotify Premium";
  if (r.status == 401) return "You're signed out. Connect Spotify again";
  if (r.status == 403) return "That action isn't allowed right now";
  if (r.status == 429) return "Spotify is rate limiting. Try again shortly";
  return "Couldn't reach Spotify";
}


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

}

Player fetchPlayer() {
  Player p;
  HttpResponse r = api(L"GET", L"/me/player?additional_types=track,episode");
  p.status = r.status;
  p.retryAfterSec = r.retryAfterSec;
  if (r.status == 204) {
    p.ok = true;
    return p;
  }
  if (r.status != 200) return p;
  Json d = Json::parse(r.body);
  p.ok = true;
  const Json& item = d["item"];
  if (!item.isObject()) return p;
  p.hasItem = true;
  p.playing = d["is_playing"].boolean();
  p.progressMs = (int64_t)d["progress_ms"].num();
  p.durationMs = (int64_t)item["duration_ms"].num();
  p.id = item["id"].str(item["uri"].str(item["name"].str()));
  p.title = item["name"].str();
  p.device = d["device"]["name"].str();
  const Json* images;
  if (item["type"].str() == "episode") {
    p.artist = item["show"]["name"].str();
    images = item["images"].size() ? &item["images"] : &item["show"]["images"];
  } else {
    for (size_t i = 0; i < item["artists"].size(); ++i) {
      if (i) p.artist += ", ";
      p.artist += item["artists"][i]["name"].str();
    }
    p.album = item["album"]["name"].str();
    images = &item["album"]["images"];
  }

  size_t pick = images->size() > 1 ? 1 : 0;
  p.artUrl = (*images)[pick]["url"].str();
  return p;
}

std::string fetchDisplayName() {
  HttpResponse r = api(L"GET", L"/me");
  if (r.status != 200) return {};
  Json d = Json::parse(r.body);
  return d["display_name"].str(d["id"].str());
}

bool decodeArt(const std::string& url, Art& out) {
  HttpResponse r = httpRequest(L"GET", widen(url));
  if (r.status != 200 || r.body.empty()) return false;

  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
    return false;
  ComPtr<IStream> stream;
  stream.Attach(SHCreateMemStream((const BYTE*)r.body.data(), (UINT)r.body.size()));
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
  HttpResponse r;
  switch (cmd) {
    case Command::Play: r = api(L"PUT", L"/me/player/play"); break;
    case Command::Pause: r = api(L"PUT", L"/me/player/pause"); break;
    case Command::Next: r = api(L"POST", L"/me/player/next"); break;
    case Command::Previous: r = api(L"POST", L"/me/player/previous"); break;
  }
  return r.status >= 200 && r.status < 300 ? std::string() : explain(r);
}

}
