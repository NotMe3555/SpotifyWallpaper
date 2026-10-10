#include "discord_bot.h"

#include <dpp/dpp.h>

#include <shlobj.h>
#include <wincrypt.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>

#include "settings.h"
#include "spotify_capture.h"
#include "util.h"

namespace discordbot {
namespace {

std::recursive_mutex g_mutex;
std::unique_ptr<dpp::cluster> g_bot;
std::atomic<dpp::cluster*> g_botRaw{nullptr};
std::atomic<uint64_t> g_voiceGuild{0};
Status g_status;
HWND g_notify = nullptr;
UINT g_msg = 0;
Output g_output = Output::Speakers;
bool g_presence = true;
bool g_commandsRegistered = false;

struct Track {
  std::string title, artist, album, art;
  bool playing = false;
} g_track;



std::mutex g_logMutex;
std::wstring g_logFile;

void logLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  if (g_logFile.empty()) {
    PWSTR local = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    g_logFile = std::wstring(local ? local : L".") + L"\\SpotifyWallpaper\\discord.log";
    CoTaskMemFree(local);
    deleteFile(g_logFile);
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stamp[16];
  snprintf(stamp, sizeof stamp, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
  std::string text;
  readFile(g_logFile, text);
  if (text.size() > 2 * 1024 * 1024) text.clear();
  writeFile(g_logFile, text + stamp + line + "\r\n");
}


struct Stats {
  std::atomic<uint64_t> chunks{0}, sent{0}, notReady{0}, backlog{0}, errors{0};
  std::atomic<double> sumSquares{0};
  std::atomic<uint64_t> samples{0};
  std::atomic<int64_t> lastReport{0};
} g_stats;

void notify() {
  if (g_notify) PostMessageW(g_notify, g_msg, 0, 0);
}

dpp::message ephemeral(const std::string& text) { return dpp::message(text).set_flags(dpp::m_ephemeral); }

std::string clip(std::string s, size_t max) {
  if (s.size() > max) s = s.substr(0, max - 3) + "...";
  return s;
}


void updatePresence() {
  dpp::cluster* bot = g_botRaw;
  if (!bot || g_status.state != State::Online) return;
  if (g_presence && g_track.playing && !g_track.title.empty())
    bot->set_presence(dpp::presence(dpp::ps_online, dpp::at_listening, clip(g_track.title + " — " + g_track.artist, 128)));
  else
    bot->set_presence(dpp::presence(dpp::ps_idle, dpp::at_watching, "for /join"));
}


void reportStats(dpp::discord_voice_client* vc) {
  int64_t now = nowMs();
  if (now - g_stats.lastReport < 5000) return;
  g_stats.lastReport = now;
  double rms = g_stats.samples ? std::sqrt(g_stats.sumSquares / g_stats.samples) : 0;
  char buf[400];
  snprintf(buf, sizeof buf,
           "stream: chunks=%llu sent=%llu notReady=%llu backlogDrops=%llu errors=%llu level=%.1f dBFS gain=%.0f "
           "voiceReady=%d e2ee=%d queued=%.2fs",
           (unsigned long long)g_stats.chunks, (unsigned long long)g_stats.sent,
           (unsigned long long)g_stats.notReady, (unsigned long long)g_stats.backlog,
           (unsigned long long)g_stats.errors, rms > 0 ? 20 * std::log10(rms) : -120.0, spotifycap::currentGain(),
           vc ? (int)vc->is_ready() : -1, vc ? (int)vc->is_end_to_end_encrypted() : -1,
           vc ? vc->get_secs_remaining() : -1.f);
  logLine(buf);
  logLine(spotifycap::takeStats());
  g_stats.chunks = g_stats.sent = g_stats.notReady = g_stats.backlog = g_stats.errors = 0;
  g_stats.sumSquares = 0;
  g_stats.samples = 0;
}

void sendAudio(const int16_t* pcm, size_t frames) {
  ++g_stats.chunks;
  double sq = 0;
  for (size_t i = 0; i < frames * 2; i += 8) sq += (pcm[i] / 32768.0) * (pcm[i] / 32768.0);
  g_stats.sumSquares = g_stats.sumSquares + sq;
  g_stats.samples += frames * 2 / 8;

  dpp::cluster* bot = g_botRaw;
  uint64_t guild = g_voiceGuild;
  dpp::discord_voice_client* client = nullptr;
  if (bot && guild)
    if (dpp::discord_client* shard = bot->get_shard(0))
      if (dpp::voiceconn* vc = shard->get_voice(guild)) client = vc->voiceclient.get();
  reportStats(client);
  if (!client || !client->is_ready()) {
    ++g_stats.notReady;
    return;
  }
  if (client->get_secs_remaining() > 1.0f) {
    ++g_stats.backlog;
    return;
  }
  try {
    client->send_audio_raw((uint16_t*)pcm, frames * 4);
    ++g_stats.sent;
  } catch (const std::exception& ex) {
    ++g_stats.errors;
    logLine(std::string("send_audio_raw failed: ") + ex.what());
  }
}

void onReady(dpp::cluster* bot) {
  if (!g_commandsRegistered) {
    g_commandsRegistered = true;
    std::vector<dpp::slashcommand> cmds = {
        dpp::slashcommand("join", "Join your voice channel and play the music from Spotify Wallpaper", bot->me.id),
        dpp::slashcommand("leave", "Leave the voice channel", bot->me.id),
        dpp::slashcommand("nowplaying", "Show the song that's playing", bot->me.id),
    };
    bot->global_bulk_command_create(cmds);
  }
  bool rejoin;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_status.state = State::Online;
    g_status.botName = bot->me.username;
    g_status.appId = bot->me.id;
    g_status.error.clear();
    rejoin = g_output != Output::Speakers;
    updatePresence();
  }

  uint64_t guild = strtoull(settings::get("discordGuild").c_str(), nullptr, 10);
  uint64_t channel = strtoull(settings::get("discordChannel").c_str(), nullptr, 10);
  logLine("ready: rejoin=" + std::to_string(rejoin) + " saved guild=" + std::to_string(guild) + " channel=" + std::to_string(channel));
  if (rejoin && guild && channel)
    if (dpp::discord_client* shard = bot->get_shard(0)) shard->connect_voice(guild, channel, false, true);
  notify();
}

void onSlashCommand(const dpp::slashcommand_t& e) {
  dpp::cluster* bot = g_botRaw;
  std::string name = e.command.get_command_name();
  if (!bot) return;
  if (name == "join") {
    dpp::guild* g = dpp::find_guild(e.command.guild_id);
    if (!g) return e.reply(ephemeral("Use /join in a server, not in DMs."));
    if (!g->connect_member_voice(*bot, e.command.get_issuing_user().id, false, true))
      return e.reply(ephemeral("Join a voice channel first, then use /join."));
    bool speakersOnly;
    {
      std::lock_guard<std::recursive_mutex> lock(g_mutex);
      speakersOnly = g_output == Output::Speakers;
    }
    e.reply(dpp::message(speakersOnly ? "Joining..."
                                      : "\U0001F3A7 "));
  } else if (name == "leave") {
    if (dpp::discord_client* shard = bot->get_shard(0)) shard->disconnect_voice(e.command.guild_id);
    settings::set("discordGuild", "");
    settings::set("discordChannel", "");
    e.reply(dpp::message("\U0001F44B Left the voice channel."));
  } else if (name == "nowplaying") {
    Track t;
    {
      std::lock_guard<std::recursive_mutex> lock(g_mutex);
      t = g_track;
    }
    if (t.title.empty()) return e.reply(ephemeral("Nothing is playing right now."));
    dpp::embed embed = dpp::embed()
                           .set_color(0x1DB954)
                           .set_title(clip(t.title, 250))
                           .set_description(clip(t.artist + (t.album.empty() ? "" : "\n*" + t.album + "*") +
                                                     (t.playing ? "" : "\n⏸ Paused"),
                                                 1000));
    if (!t.art.empty()) embed.set_thumbnail(t.art);
    e.reply(dpp::message().add_embed(embed));
  }
}

void setVoice(uint64_t guild, uint64_t channel) {
  std::string guildName, channelName;
  if (guild) {
    if (dpp::guild* g = dpp::find_guild(guild)) guildName = g->name;
    if (dpp::channel* c = dpp::find_channel(channel)) channelName = c->name;
  }
  g_voiceGuild = guild;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_status.guild = guild ? (guildName.empty() ? "a server" : guildName) : "";
    g_status.channel = guild ? (channelName.empty() ? "voice" : channelName) : "";
  }
  notify();
}

}

namespace {



void installCaBundle() {
  static bool done = false;
  if (done) return;
  done = true;
  std::string pem;
  for (const wchar_t* storeName : {L"ROOT", L"AuthRoot", L"CA"}) {
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, storeName);
    if (!store) continue;
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert))) {
      DWORD len = 0;
      if (!CryptBinaryToStringA(cert->pbCertEncoded, cert->cbCertEncoded, CRYPT_STRING_BASE64HEADER, nullptr, &len))
        continue;
      std::string block(len, '\0');
      if (CryptBinaryToStringA(cert->pbCertEncoded, cert->cbCertEncoded, CRYPT_STRING_BASE64HEADER, block.data(),
                               &len)) {
        block.resize(len);
        pem += block;
      }
    }
    CertCloseStore(store, 0);
  }
  PWSTR local = nullptr;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
  std::wstring path = std::wstring(local ? local : L".") + L"\\SpotifyWallpaper\\cacert.pem";
  CoTaskMemFree(local);
  if (pem.empty() || !writeFile(path, pem)) {
    logLine("couldn't export Windows root certificates");
    return;
  }
  SetEnvironmentVariableW(L"SSL_CERT_FILE", path.c_str());
  _wputenv_s(L"SSL_CERT_FILE", path.c_str());
  logLine("trusted root certificates exported for OpenSSL (" + std::to_string(pem.size() / 1024) + " KB)");
}

}

void start(const std::string& token, HWND notifyHwnd, UINT msg) {
  installCaBundle();
  stop();
  g_notify = notifyHwnd;
  g_msg = msg;
  if (token.empty()) return;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_status = {};
    g_status.state = State::Connecting;
  }
  g_commandsRegistered = false;
  try {
    auto bot = std::make_unique<dpp::cluster>(token, dpp::i_default_intents);
    bot->on_log([](const dpp::log_t& e) {
      if (e.severity >= dpp::ll_info ||
          (e.severity == dpp::ll_debug && (e.message.find("oice") != std::string::npos ||
                                           e.message.find("DAVE") != std::string::npos ||
                                           e.message.find("MLS") != std::string::npos)))
        logLine("[level " + std::to_string((int)e.severity) + "] " + e.message.substr(0, 600));
      if (e.severity < dpp::ll_error) return;
      std::lock_guard<std::recursive_mutex> lock(g_mutex);
      g_status.error = e.message;
      if (g_status.state == State::Connecting) g_status.state = State::Error;
      notify();
    });
    bot->on_ready([](const dpp::ready_t& e) { onReady(e.owner); });
    bot->on_slashcommand(onSlashCommand);
    bot->on_voice_ready([](const dpp::voice_ready_t& e) {
      if (!e.voice_client) return;
      logLine("voice ready: e2ee=" + std::to_string(e.voice_client->is_end_to_end_encrypted()) +
              " privacy code=" + e.voice_client->get_privacy_code());
      uint64_t guild = e.voice_client->server_id, channel = e.voice_client->channel_id;
      settings::set("discordGuild", std::to_string(guild));
      settings::set("discordChannel", std::to_string(channel));
      setVoice(guild, channel);
    });
    bot->on_voice_client_disconnect([](const dpp::voice_client_disconnect_t&) { setVoice(0, 0); });
    bot->on_voice_state_update([](const dpp::voice_state_update_t& e) {
      dpp::cluster* bot = g_botRaw;
      if (!bot || e.state.user_id != bot->me.id) return;
      if (!e.state.channel_id) setVoice(0, 0);
      else if (g_voiceGuild) setVoice(e.state.guild_id, e.state.channel_id);
    });
    g_botRaw = bot.get();
    g_bot = std::move(bot);
    g_bot->start(dpp::st_return);
  } catch (const std::exception& ex) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_status.state = State::Error;
    g_status.error = ex.what();
  }
  notify();
}

void stop() {
  spotifycap::stop();
  std::unique_ptr<dpp::cluster> bot;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    bot = std::move(g_bot);
    g_botRaw = nullptr;
    g_voiceGuild = 0;
    g_status = {};
  }
  if (bot) {
    try {
      bot->shutdown();
    } catch (...) {
    }
  }
  bot.reset();
  notify();
}

Status status() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  Status s = g_status;
  s.streaming = spotifycap::running();
  return s;
}


void sync() {
  Output out;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    out = g_output;
  }
  bool want = out != Output::Speakers && g_voiceGuild != 0 && g_botRaw != nullptr;
  spotifycap::setMuteLocal(out == Output::Discord);
  if (want && !spotifycap::running()) spotifycap::start(sendAudio);
  if (!want && spotifycap::running()) spotifycap::stop();
}

void setOutput(Output output) {
  bool rejoin;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    rejoin = g_output == Output::Speakers && output != Output::Speakers && g_status.state == State::Online;
    g_output = output;
  }

  uint64_t guild = strtoull(settings::get("discordGuild").c_str(), nullptr, 10);
  uint64_t channel = strtoull(settings::get("discordChannel").c_str(), nullptr, 10);
  dpp::cluster* bot = g_botRaw;
  if (rejoin && bot && !g_voiceGuild && guild && channel)
    if (dpp::discord_client* shard = bot->get_shard(0)) shard->connect_voice(guild, channel, false, true);
  sync();
}

void setPresenceEnabled(bool on) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  g_presence = on;
  updatePresence();
}

void setNowPlaying(const std::string& title, const std::string& artist, const std::string& album,
                   const std::string& artUrl, bool playing) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  bool changed = title != g_track.title || artist != g_track.artist || playing != g_track.playing;
  g_track = {title, artist, album, artUrl, playing};
  if (changed) updatePresence();
}

void leaveVoice() {
  dpp::cluster* bot = g_botRaw;
  uint64_t guild = g_voiceGuild;
  if (bot && guild)
    if (dpp::discord_client* shard = bot->get_shard(0)) shard->disconnect_voice(guild);
  settings::set("discordGuild", "");
  settings::set("discordChannel", "");
  setVoice(0, 0);
}

std::string inviteUrl() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!g_status.appId) return {};

  return "https://discord.com/oauth2/authorize?client_id=" + std::to_string(g_status.appId) +
         "&scope=bot%20applications.commands&permissions=3145728";
}

}
