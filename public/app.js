(() => {
  const POLL_MS = 2000;
  const POLL_HIDDEN_MS = 10000;

  const $ = (id) => document.getElementById(id);
  const body = document.body;
  const el = {
    clockTime: $('clockTime'), clockDate: $('clockDate'),
    artA: $('artA'), artB: $('artB'), bgA: $('bgA'), bgB: $('bgB'),
    status: $('status'), title: $('title'), artist: $('artist'), album: $('album'),
    progressTrack: $('progressTrack'), progressFill: $('progressFill'),
    elapsed: $('elapsed'), duration: $('duration'),
    prevBtn: $('prevBtn'), playBtn: $('playBtn'), nextBtn: $('nextBtn'),
    muteBtn: $('muteBtn'), volume: $('volume'), volWave1: $('volWave1'), volWave2: $('volWave2'),
    device: $('device'), toast: $('toast'),
    bgCustom: $('bgCustom'), bgVideo: $('bgVideo'), artVideo: $('artVideo'), vizBars: $('vizBars'), vizRing: $('vizRing'),
    lyricsPlayerPrevious: $('lyricsPlayerPrevious'), lyricsPlayerActive: $('lyricsPlayerActive'),
    lyricsPlayerNext: $('lyricsPlayerNext'), lyricsScreenPrevious: $('lyricsScreenPrevious'),
    lyricsScreenActive: $('lyricsScreenActive'), lyricsScreenNext: $('lyricsScreenNext'),
  };

  const state = {
    itemId: null,
    item: null,
    isPlaying: false,
    progress: 0,
    duration: 0,
    syncedAt: 0,
    volume: null,
    volumeLockUntil: 0,
    playLockUntil: 0,
    lastVolume: 50,
    artFront: el.artA,
    bgFront: el.bgA,
  };

  const lyricState = { key: '', pending: '', lines: [], plain: false, shownIndex: -2, message: '' };


  const ACCENTS = {
    white: '232 234 240', blue: '130 175 255', green: '110 220 160', pink: '255 145 195',
    orange: '255 175 105', purple: '185 155 255', red: '255 125 125',
  };
  const FLAGS = ['clock', 'date', 'art', 'album', 'progress', 'controls', 'volume', 'device', 'idleFade', 'motion', 'grain', 'vignette'];
  let settings = {};
  let lastArtImg = null;

  // The user's own backgrounds: an image stays for the chosen time, a video plays through, then the next fades in.
  const customBg = { key: '', files: [], index: 0, seconds: 15, timer: null, current: null };
  const CUSTOM_FADE_MS = 1200;

  function showCustomBg(index, failures = 0) {
    clearTimeout(customBg.timer);
    const { files, key } = customBg;
    if (!files.length) return;
    index = ((index % files.length) + files.length) % files.length;
    customBg.index = index;
    const isVideo = /\.(mp4|webm)$/i.test(files[index]);
    const media = document.createElement(isVideo ? 'video' : 'img');
    const next = () => { if (customBg.key === key && customBg.current === media) showCustomBg(index + 1); };
    const reveal = () => {
      if (customBg.key !== key) return media.remove();
      const old = customBg.current;
      customBg.current = media;
      void media.offsetWidth;
      media.classList.add('is-visible');
      if (old) setTimeout(() => old.remove(), CUSTOM_FADE_MS + 100);
      if (!isVideo && files.length > 1) customBg.timer = setTimeout(next, customBg.seconds * 1000);
    };
    const skip = () => {
      media.remove();
      if (customBg.key === key && failures + 1 < files.length) showCustomBg(index + 1, failures + 1);
    };
    if (isVideo) {
      Object.assign(media, { muted: true, autoplay: true, playsInline: true, loop: files.length === 1 });
      media.addEventListener('loadeddata', reveal, { once: true });
      media.addEventListener('ended', next);
    } else {
      media.addEventListener('load', reveal, { once: true });
    }
    media.addEventListener('error', skip, { once: true });
    media.src = '/background?i=' + index + '&v=' + encodeURIComponent(files[index]);
    el.bgCustom.appendChild(media);
  }

  function updateCustomBg() {
    const files = settings.background === 'custom' ? (settings.bgFiles || '').split('|').filter(Boolean) : [];
    const key = files.join('|');
    const seconds = Math.max(1, parseInt(settings.bgImageSeconds, 10) || 15);
    const secondsChanged = seconds !== customBg.seconds;
    customBg.seconds = seconds;
    if (key === customBg.key) {
      const onImage = customBg.current && customBg.current.tagName === 'IMG';
      if (secondsChanged && onImage && files.length > 1) {
        clearTimeout(customBg.timer);
        customBg.timer = setTimeout(() => showCustomBg(customBg.index + 1), seconds * 1000);
      }
      return;
    }
    clearTimeout(customBg.timer);
    Object.assign(customBg, { key, files, index: 0, current: null });
    el.bgCustom.innerHTML = '';
    if (files.length) showCustomBg(0);
  }

  function applySettings(next) {
    if (!next || typeof next !== 'object') return;
    settings = next;
    for (const key of FLAGS) body.classList.toggle('no-' + key, next[key] === 'off');
    body.dataset.position = next.position || 'center';
    body.dataset.shape = next.artShape || 'rounded';
    body.dataset.bg = next.background || 'art';
    body.dataset.brightness = next.brightness || 'normal';
    body.dataset.blur = next.blur || 'normal';
    body.dataset.videoMode = next.videoMode === 'cover' ? 'cover' : 'background';
    body.dataset.viz = next.visualizer || 'off';
    body.dataset.lyrics = next.lyrics || 'off';
    body.dataset.lyricsSize = next.lyricsSize || 'normal';
    const root = document.documentElement.style;
    root.setProperty('--bg', next.background === 'black' ? '#000000' : next.bgColor || '#07080b');
    root.setProperty('--bg-color2', next.bgColor2 || '#1d1033');
    document.documentElement.style.setProperty('--scale', String((parseInt(next.scale, 10) || 100) / 100));
    const videoBright = parseInt(next.videoBrightness, 10);
    root.setProperty('--video-bright', String((Number.isFinite(videoBright) ? videoBright : 75) / 100));
    if (ACCENTS[next.accent]) document.documentElement.style.setProperty('--accent', ACCENTS[next.accent]);
    else if (lastArtImg) extractAccent(lastArtImg);
    else document.documentElement.style.setProperty('--accent', '170 185 210');
    if (next.idleFade === 'off') body.classList.remove('is-idle');
    updateCustomBg();
    updateClock();
    updateVideo(state.item);
    requestLyrics(state.item);
    startViz();
  }
  window.applySettings = applySettings;

  async function loadSettings() {
    try {
      const res = await fetch('/settings', { cache: 'no-store' });
      if (res.ok) {
        const next = await res.json();
        if (JSON.stringify(next) !== JSON.stringify(settings)) applySettings(next);
      }
    } catch {  }
  }


  class ApiError extends Error {
    constructor(status, message, reason) { super(message); this.status = status; this.reason = reason; }
  }

  // Playback is driven through the Spotify app on this PC; the wallpaper never calls Spotify's web API.
  async function control(cmd, params = {}) {
    const q = new URLSearchParams({ cmd, ...params });
    const res = await fetch('/control?' + q, { cache: 'no-store', headers: { 'X-Wallpaper': '1' } });
    if (!res.ok) throw new ApiError(res.status, `HTTP ${res.status}`);
  }

  async function playerState() {
    const res = await fetch('/player', { cache: 'no-store' });
    if (!res.ok) {
      let err = {};
      try { err = (await res.json()).error || {}; } catch {}
      throw new ApiError(res.status, err.message || `HTTP ${res.status}`, err.reason);
    }
    const { age, player } = await res.json();
    if (player && player.is_playing && player.progress_ms != null) player.progress_ms += age;
    return player;
  }


  const fmt = (ms) => {
    const s = Math.max(0, Math.floor(ms / 1000));
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
  };

  let toastTimer;
  function toast(msg) {
    el.toast.textContent = msg;
    el.toast.classList.add('is-visible');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.toast.classList.remove('is-visible'), 3200);
  }

  function explain(err) {
    if (err.status === 409) return 'Open Spotify on this PC to control playback';
    return 'Couldn’t reach the Spotify Wallpaper app';
  }

  function isNetworkFailure(err) {
    return err instanceof TypeError;
  }

  function swapText(node, text) {
    if (node.textContent === text) return;
    node.textContent = text;
    node.classList.remove('swap');
    void node.offsetWidth;
    node.classList.add('swap');
  }


  function extractAccent(img) {
    if (ACCENTS[settings.accent]) return;
    try {
      const c = document.createElement('canvas');
      c.width = c.height = 24;
      const ctx = c.getContext('2d', { willReadFrequently: true });
      ctx.drawImage(img, 0, 0, 24, 24);
      const d = ctx.getImageData(0, 0, 24, 24).data;
      let r = 0, g = 0, b = 0, w = 0;
      for (let i = 0; i < d.length; i += 4) {
        const max = Math.max(d[i], d[i + 1], d[i + 2]);
        const min = Math.min(d[i], d[i + 1], d[i + 2]);
        const weight = 1 + (max - min) / 32 + (max > 40 ? 1 : 0);
        r += d[i] * weight; g += d[i + 1] * weight; b += d[i + 2] * weight; w += weight;
      }
      let [h, s, l] = rgbToHsl(r / w, g / w, b / w);
      s = Math.min(s, 0.5);
      l = 0.68;
      const [R, G, B] = hslToRgb(h, s, l);
      document.documentElement.style.setProperty('--accent', `${R} ${G} ${B}`);
    } catch {  }
  }

  function rgbToHsl(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b);
    let h = 0, s = 0;
    const l = (max + min) / 2;
    if (max !== min) {
      const d = max - min;
      s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
      h = max === r ? (g - b) / d + (g < b ? 6 : 0) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
      h /= 6;
    }
    return [h, s, l];
  }

  function hslToRgb(h, s, l) {
    if (s === 0) return [l, l, l].map((v) => Math.round(v * 255));
    const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const p = 2 * l - q;
    const f = (t) => {
      if (t < 0) t += 1; if (t > 1) t -= 1;
      if (t < 1 / 6) return p + (q - p) * 6 * t;
      if (t < 1 / 2) return q;
      if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
      return p;
    };
    return [f(h + 1 / 3), f(h), f(h - 1 / 3)].map((v) => Math.round(v * 255));
  }

  function setArt(url) {
    const back = state.artFront === el.artA ? el.artB : el.artA;
    const bgBack = state.bgFront === el.bgA ? el.bgB : el.bgA;
    if (!url) {
      el.artA.classList.remove('is-visible');
      el.artB.classList.remove('is-visible');
      el.bgA.classList.remove('is-visible');
      el.bgB.classList.remove('is-visible');
      return;
    }
    const img = new Image();
    img.crossOrigin = 'anonymous';
    img.onload = () => {
      lastArtImg = img;
      back.src = url;
      back.classList.add('is-visible');
      state.artFront.classList.remove('is-visible');
      state.artFront = back;

      bgBack.style.backgroundImage = `url("${url}")`;
      bgBack.classList.add('is-visible');
      state.bgFront.classList.remove('is-visible');
      state.bgFront = bgBack;

      extractAccent(img);
    };
    img.src = url;
  }

  const position = () => {
    let pos = state.progress;
    if (state.isPlaying) pos += performance.now() - state.syncedAt;
    return Math.min(pos, state.duration || pos);
  };









  const video = {
    key: null, mode: null, id: null, player: null, shown: false, failed: new Set(),
    candidates: [], playingSince: 0, revealTimer: null, revealDelay: 0,
    loading: false, noMatch: false, retryAt: 0, lastTime: -1, stuckSince: 0,
    seekAt: 0, settling: false, lead: 0.5,
  };
  const REVEAL_AFTER_MS = 3500;
  const REVEAL_AGAIN_MS = 1200;
  const FADE_OUT_MS = 400;
  const END_MARGIN_S = 7;
  const SEEK_DRIFT_S = 3;
  const NUDGE_DRIFT_S = 0.4;
  const SEEK_COOLDOWN_MS = 5000;
  let ytApi = null;

  // YouTube picks its stream from the player's size, so a full 4K-wide iframe pulls 4K and stalls.
  const VIDEO_W = 1920, VIDEO_H = 1080;
  function fitVideo() {
    const fit = Math.max(innerWidth / VIDEO_W, innerHeight / VIDEO_H) * 1.2;
    document.documentElement.style.setProperty('--video-fit', String(fit));
  }
  fitVideo();
  window.addEventListener('resize', fitVideo);

  function loadYouTubeApi() {
    if (!ytApi) {
      ytApi = new Promise((resolve, reject) => {
        window.onYouTubeIframeAPIReady = () => resolve(window.YT);
        const script = document.createElement('script');
        script.src = 'https://www.youtube.com/iframe_api';
        script.onerror = () => { ytApi = null; reject(new Error('YouTube unavailable')); };
        document.head.appendChild(script);
      });
    }
    return ytApi;
  }

  const playerReady = (p) => !!p && typeof p.getPlayerState === 'function';
  const isMoving = (p) => playerReady(p) && p.getPlayerState() === YT.PlayerState.PLAYING;
  const nearEnd = (p) => {
    const length = p.getDuration() || 0;
    return length > 0 && (p.getCurrentTime() || 0) > length - END_MARGIN_S;
  };

  function resetReveal(delay) {
    video.shown = false;
    video.playingSince = 0;
    video.revealDelay = delay;
    clearTimeout(video.revealTimer);
  }


  function cutVideo(brief) {
    resetReveal(brief ? REVEAL_AGAIN_MS : REVEAL_AFTER_MS);
    if (!body.classList.contains('has-video')) return;
    body.classList.add('video-cut');
    body.classList.remove('has-video');
    void body.offsetWidth;
    requestAnimationFrame(() => requestAnimationFrame(() => body.classList.remove('video-cut')));
  }


  function fadeVideo(after) {
    const wasShown = body.classList.contains('has-video');
    resetReveal(REVEAL_AFTER_MS);
    body.classList.remove('has-video');
    if (after) setTimeout(after, wasShown ? FADE_OUT_MS + 50 : 0);
  }

  function pauseQuietly() {
    try { if (playerReady(video.player) && !video.shown) video.player.pauseVideo(); } catch {}
  }

  function destroyVideo() {
    cutVideo(false);
    try { video.player && video.player.destroy(); } catch {}
    video.player = null;
    video.id = null;
    el.bgVideo.innerHTML = '';
    el.artVideo.innerHTML = '';
  }

  async function updateVideo(item) {
    const on = settings.video === 'on' && item && item.type === 'track';
    const mode = settings.videoMode === 'cover' ? 'cover' : 'background';
    const key = on ? item.id + '|' + mode : null;
    if (key === video.key) return;
    video.key = key;
    if (!on) return destroyVideo();
    if (video.mode !== mode) { destroyVideo(); video.mode = mode; }
    fadeVideo();
    video.noMatch = false;
    video.loading = true;
    video.retryAt = performance.now() + 30000;
    video.stuckSince = 0;

    let ids = [], lookupError = '';
    try {
      const q = new URLSearchParams({ artist: item.artists.map((a) => a.name).join(', '), title: item.name });
      const res = await fetch('/video?' + q, { cache: 'no-store' });
      const info = await res.json();
      ids = info.ids || [];
      lookupError = info.error || '';
    } catch { lookupError = 'fetch'; }
    if (video.key !== key) return;
    video.loading = false;
    ids = ids.filter((v) => v && !video.failed.has(v));
    if (!ids.length) {
      video.noMatch = !lookupError;
      video.id = null;
      pauseQuietly();
      return;
    }
    video.candidates = ids.slice(1);
    const id = ids[0];

    let YT;
    try { YT = await loadYouTubeApi(); } catch { return; }
    if (video.key !== key) return;

    const start = position() / 1000;
    video.id = id;
    if (playerReady(video.player)) {
      cutVideo(false);
      video.player.loadVideoById({ videoId: id, startSeconds: start });
      return;
    }
    if (video.player) return;
    const host = document.createElement('div');
    (mode === 'cover' ? el.artVideo : el.bgVideo).appendChild(host);
    video.player = new YT.Player(host, {
      videoId: id,
      playerVars: {
        autoplay: 1, mute: 1, controls: 0, disablekb: 1, fs: 0, iv_load_policy: 3,
        playsinline: 1, rel: 0, start: Math.floor(start),
        origin: location.origin, enablejsapi: 1,
        cc_load_policy: 0,
      },
      events: {
        onReady: (e) => {
          e.target.mute();
          hideCaptions(e.target);
          if (video.id !== id) e.target.loadVideoById({ videoId: video.id, startSeconds: position() / 1000 });
          syncVideo(true);
        },
        onStateChange: (e) => {
          if (e.data === YT.PlayerState.PLAYING) {
            hideCaptions(e.target);
            if (video.key) startReveal();
          } else {
            cutVideo(e.data === YT.PlayerState.BUFFERING);
          }
        },
        onError: (e) => {
          fetch('/log?msg=' + encodeURIComponent('video ' + video.id + ' error ' + e.data)).catch(() => {});
          video.failed.add(video.id);
          cutVideo(false);
          const next = video.candidates.shift();
          if (next && video.player) {
            video.id = next;
            video.player.loadVideoById({ videoId: next, startSeconds: position() / 1000 });
          }
        },
      },
    });
  }


  function startReveal() {
    if (video.playingSince || video.shown) return;
    video.playingSince = performance.now();
    clearTimeout(video.revealTimer);
    video.revealTimer = setTimeout(() => {
      const p = video.player;
      if (!video.playingSince || !video.key || video.loading || !state.isPlaying || !isMoving(p) || nearEnd(p)) {
        video.playingSince = 0;
        return;
      }
      video.shown = true;
      body.classList.add('has-video');
    }, video.revealDelay || REVEAL_AFTER_MS);
  }


  function videoGuard() {
    if (!video.shown) return;
    const p = video.player;
    if (!isMoving(p)) return cutVideo(playerReady(p) && p.getPlayerState() === YT.PlayerState.BUFFERING);
    if (nearEnd(p)) fadeVideo();
  }
  setInterval(videoGuard, 150);




  function videoWatchdog() {
    if (document.hidden || video.loading || settings.video !== 'on' || !state.item || state.item.type !== 'track') return;
    const now = performance.now();
    const p = video.player;
    if (!playerReady(p) || !video.id) {

      if (!video.loading && !video.noMatch && now > video.retryAt) {
        video.key = null;
        updateVideo(state.item);
      }
      return;
    }
    if (!state.isPlaying || video.failed.has(video.id)) { video.stuckSince = 0; return; }
    const length = p.getDuration() || 0;
    if (length && position() / 1000 > length - END_MARGIN_S) return;
    const t = p.getCurrentTime() || 0;
    const moving = isMoving(p) && t !== video.lastTime;
    video.lastTime = t;
    if (moving) {
      video.stuckSince = 0;
      const drift = t - position() / 1000;
      const sinceSeek = now - video.seekAt;
      if (video.settling) {
        if (sinceSeek < 1000) return;
        video.settling = false;
        video.lead = Math.max(0, Math.min(5, video.lead - drift));
      }
      if (Math.abs(drift) > SEEK_DRIFT_S && sinceSeek > SEEK_COOLDOWN_MS) {
        cutVideo(true);
        seekVideo(p);
        return;
      }
      nudgeVideo(p, drift);
      startReveal();
      return;
    }
    cutVideo(false);
    if (!video.stuckSince) { video.stuckSince = now; return; }
    const stuckFor = now - video.stuckSince;
    if (stuckFor > 20000) {
      video.stuckSince = 0;
      video.key = null;
      destroyVideo();
      updateVideo(state.item);
    } else if (stuckFor > 2500 && now - video.seekAt > SEEK_COOLDOWN_MS) {
      video.lead = Math.min(5, video.lead + 1.5);
      try { seekVideo(p); p.playVideo(); } catch {}
    }
  }
  setInterval(videoWatchdog, 750);

  // Seeking rebuffers, so aim ahead by however long the last seek took to start playing.
  function seekVideo(p) {
    video.seekAt = performance.now();
    video.settling = true;
    p.seekTo(position() / 1000 + video.lead, true);
  }

  // Small drift is closed by playing slightly fast or slow instead of rebuffering.
  function nudgeVideo(p, drift) {
    const off = Math.abs(drift);
    const current = p.getPlaybackRate();
    let rate = 1;
    if (off > (current === 1 ? NUDGE_DRIFT_S : 0.1)) {
      rate = drift > 0 ? (off > 1.5 ? 0.5 : 0.75) : (off > 1.5 ? 1.5 : 1.25);
    }
    if (rate !== current) p.setPlaybackRate(rate);
  }


  function hideCaptions(p) {
    try {
      p.setOption('captions', 'track', {});
      p.unloadModule('captions');
      p.unloadModule('cc');
    } catch {}
  }


  function syncVideo(force) {
    const p = video.player;
    if (!playerReady(p) || !video.id || video.failed.has(video.id)) return;
    const target = position() / 1000;
    const length = p.getDuration() || 0;
    if (length && target > length - END_MARGIN_S) {
      if (video.shown) fadeVideo(pauseQuietly);
      return;
    }
    const playing = isMoving(p);
    if (!state.isPlaying) {

      if (playing) fadeVideo(() => { if (!state.isPlaying) pauseQuietly(); });
      return;
    }
    if (!playing) p.playVideo();
    if (force) {
      if (video.shown) cutVideo(true);
      seekVideo(p);
    }
  }


  const BANDS = 48;
  const viz = { target: new Float32Array(BANDS), shown: new Float32Array(BANDS), lastData: 0, running: false };
  const GAIN = { low: 0.7, normal: 1, high: 1.35 };

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', (e) => {
      const d = e.data;
      if (d && d.t === 'spectrum' && Array.isArray(d.b)) {
        viz.target.set(d.b.slice(0, BANDS));
        viz.lastData = performance.now();
      }
    });
  }

  function fitCanvas(c) {
    const r = c.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    return { w, h, dpr };
  }

  function makePalette() {
    if (settings.vizColor === 'rainbow') return (i, n, a) => 'hsla(' + Math.round((i / n) * 360) + ',85%,65%,' + a + ')';
    if (settings.vizColor === 'white') return (i, n, a) => 'rgba(240,242,246,' + a + ')';
    const rgb = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim().split(/\s+/).join(',');
    return (i, n, a) => 'rgba(' + rgb + ',' + a + ')';
  }


  function drawRing(values, color) {
    const c = el.vizRing;
    const { w, h, dpr } = fitCanvas(c);
    const ctx = c.getContext('2d');
    ctx.clearRect(0, 0, w, h);
    const cx = w / 2, cy = h / 2, art = w / 1.7;
    const round = body.dataset.shape === 'circle' || body.dataset.shape === 'vinyl';
    const total = values.length * 2;
    const half = art / 2 + 7 * dpr;
    const perimeter = round ? 2 * Math.PI * half : 8 * half;
    ctx.lineCap = 'round';
    ctx.lineWidth = (perimeter / total) * 0.55;
    for (let i = 0; i < total; i++) {
      const v = values[i < values.length ? i : total - 1 - i];
      const len = 2 * dpr + v * art * 0.3;
      let x, y, nx, ny;
      if (round) {
        const a = (i / total) * Math.PI * 2 - Math.PI / 2;
        nx = Math.cos(a); ny = Math.sin(a);
        x = cx + nx * half; y = cy + ny * half;
      } else {
        const s = ((i / total) * perimeter + half) % perimeter;
        const edge = Math.floor(s / (2 * half)), t = s - edge * 2 * half;
        if (edge === 0) { x = cx - half + t; y = cy - half; nx = 0; ny = -1; }
        else if (edge === 1) { x = cx + half; y = cy - half + t; nx = 1; ny = 0; }
        else if (edge === 2) { x = cx + half - t; y = cy + half; nx = 0; ny = 1; }
        else { x = cx - half; y = cy + half - t; nx = -1; ny = 0; }
      }
      ctx.strokeStyle = color(i, total, (0.45 + 0.55 * v).toFixed(2));
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x + nx * len, y + ny * len);
      ctx.stroke();
    }
  }


  function drawBars(values, color) {
    const c = el.vizBars;
    const { w, h, dpr } = fitCanvas(c);
    const ctx = c.getContext('2d');
    ctx.clearRect(0, 0, w, h);
    const n = values.length, total = n * 2, step = w / total, bw = step * 0.62;
    for (let i = 0; i < total; i++) {
      const v = values[i < n ? n - 1 - i : i - n];
      const bh = Math.max(2 * dpr, v * h * 0.92);
      const x = i * step + (step - bw) / 2;
      const grad = ctx.createLinearGradient(0, h - bh, 0, h);
      grad.addColorStop(0, color(i, total, 0.9));
      grad.addColorStop(1, color(i, total, 0.05));
      ctx.fillStyle = grad;
      ctx.beginPath();
      if (ctx.roundRect) ctx.roundRect(x, h - bh, bw, bh, [bw / 2, bw / 2, 0, 0]);
      else ctx.rect(x, h - bh, bw, bh);
      ctx.fill();
    }
  }

  function vizFrame(now) {
    const mode = settings.visualizer || 'off';
    if (mode === 'off') {
      viz.running = false;
      for (const c of [el.vizRing, el.vizBars]) c.getContext('2d').clearRect(0, 0, c.width, c.height);
      return;
    }
    const stale = now - viz.lastData > 400;
    const gain = GAIN[settings.vizIntensity] || 1;
    for (let i = 0; i < BANDS; i++) {
      const t = stale ? 0 : Math.min(1, Math.pow(viz.target[i], 1.35) * gain);
      viz.shown[i] += (t - viz.shown[i]) * 0.35;
    }
    const color = makePalette();
    if (mode === 'ring' || mode === 'both') drawRing(viz.shown, color);
    if (mode === 'bars' || mode === 'both') drawBars(viz.shown, color);
    requestAnimationFrame(vizFrame);
  }

  function startViz() {
    if (viz.running || !settings.visualizer || settings.visualizer === 'off') return;
    viz.running = true;
    requestAnimationFrame(vizFrame);
  }

  function clearLyrics() {
    lyricState.key = '';
    lyricState.pending = '';
    lyricState.lines = [];
    lyricState.plain = false;
    lyricState.shownIndex = -2;
    lyricState.message = '';
    renderLyrics(true);
  }

  function parseLyrics(data) {
    const synced = [];
    const stamp = /\[(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\]/g;
    for (const raw of (data.synced || '').split(/\r?\n/)) {
      const text = raw.replace(stamp, '').trim();
      if (!text) continue;
      stamp.lastIndex = 0;
      let match;
      while ((match = stamp.exec(raw))) {
        const fraction = (match[3] || '0').padEnd(3, '0').slice(0, 3);
        synced.push({ time: (+match[1] * 60 + +match[2]) * 1000 + +fraction, text });
      }
    }
    if (synced.length) return { lines: synced.sort((a, b) => a.time - b.time), plain: false };
    const plain = (data.plain || '').split(/\r?\n/).map((text) => text.trim()).filter(Boolean);
    return { lines: plain.map((text) => ({ time: 0, text })), plain: plain.length > 0 };
  }

  async function requestLyrics(item) {
    if (!['player', 'screen'].includes(settings.lyrics) || !item || item.type !== 'track') {
      if (!['player', 'screen'].includes(settings.lyrics) || !item) clearLyrics();
      return;
    }
    const key = item.id;
    if (lyricState.key === key || lyricState.pending === key) return;
    lyricState.pending = key;
    lyricState.message = 'Finding lyrics…';
    renderLyrics(true);
    const query = new URLSearchParams({
      artist: (item.artists || []).map((artist) => artist.name).join(', '),
      title: item.name || '',
      album: item.album?.name || '',
      duration: String(Math.round((item.duration_ms || 0) / 1000)),
    });
    try {
      const response = await fetch('/lyrics?' + query, { cache: 'no-store' });
      if (!response.ok) throw new Error('Lyrics request failed');
      const data = await response.json();
      if (state.itemId !== key || settings.lyrics === 'off') return;
      const parsed = parseLyrics(data);
      lyricState.key = key;
      lyricState.lines = parsed.lines;
      lyricState.plain = parsed.plain;
      lyricState.message = data.instrumental ? 'Instrumental' : parsed.lines.length ? '' : 'Lyrics aren’t available for this song';
      lyricState.shownIndex = -2;
      renderLyrics(true);
    } catch {
      if (state.itemId === key) {
        lyricState.key = key;
        lyricState.lines = [];
        lyricState.message = 'Couldn’t load lyrics';
        renderLyrics(true);
      }
    } finally {
      if (lyricState.pending === key) lyricState.pending = '';
    }
  }

  function renderLyrics(force = false) {
    const nodes = [
      [el.lyricsPlayerPrevious, el.lyricsScreenPrevious],
      [el.lyricsPlayerActive, el.lyricsScreenActive],
      [el.lyricsPlayerNext, el.lyricsScreenNext],
    ];
    let index = -1;
    if (lyricState.lines.length) {
      if (lyricState.plain) {
        index = Math.min(lyricState.lines.length - 1,
          Math.floor((position() / Math.max(1, state.duration)) * lyricState.lines.length));
      } else {
        const now = position();
        let low = 0, high = lyricState.lines.length - 1;
        while (low <= high) {
          const mid = (low + high) >> 1;
          if (lyricState.lines[mid].time <= now) low = mid + 1;
          else high = mid - 1;
        }
        index = high;
      }
    }
    const signature = lyricState.lines.length ? `${lyricState.key}:${index}` : `${lyricState.pending}:${lyricState.message}`;
    if (!force && signature === lyricState.shownIndex) return;
    lyricState.shownIndex = signature;
    if (!lyricState.lines.length) {
      nodes[0].forEach((node) => { node.textContent = ''; });
      nodes[1].forEach((node) => { node.textContent = lyricState.message; });
      nodes[2].forEach((node) => { node.textContent = ''; });
      return;
    }
    const current = Math.max(0, index);
    const texts = [lyricState.lines[current - 1]?.text || '', lyricState.lines[current]?.text || '',
      lyricState.lines[current + 1]?.text || ''];
    nodes.forEach((pair, i) => pair.forEach((node) => { node.textContent = texts[i]; }));
  }


  function renderPlaying(isPlaying) {
    body.classList.toggle('is-playing', isPlaying);
    body.classList.toggle('is-paused', !isPlaying && !!state.itemId);
    el.status.textContent = state.itemId ? (isPlaying ? 'Now playing' : 'Paused') : 'Nothing playing';
  }

  function renderVolume(v) {
    el.volume.value = v;
    el.volume.style.setProperty('--val', `${v}%`);
    el.volWave1.style.opacity = v > 0 ? 1 : 0.15;
    el.volWave2.style.opacity = v > 50 ? 1 : 0.15;
  }

  function renderEmpty() {
    if (state.itemId === null && body.classList.contains('is-empty')) return;
    state.itemId = null;
    state.isPlaying = false;
    state.duration = 0;
    state.progress = 0;
    body.classList.add('is-empty');
    swapText(el.title, 'Quiet for now');
    swapText(el.artist, 'Start something on Spotify');
    swapText(el.album, '');
    el.device.textContent = '';
    setArt(null);
    renderPlaying(false);
    state.item = null;
    clearLyrics();
    updateVideo(null);
  }

  function render(data) {
    const item = data && data.item;
    if (!item) return renderEmpty();

    body.classList.remove('is-empty');

    if (item.id !== state.itemId) {
      state.itemId = item.id;
      const isEpisode = item.type === 'episode';
      const images = isEpisode ? (item.images || item.show?.images) : item.album?.images;
      swapText(el.title, item.name);
      swapText(el.artist, isEpisode ? item.show?.name || '' : item.artists.map((a) => a.name).join(', '));
      swapText(el.album, isEpisode ? '' : item.album?.name || '');
      setArt(images && images[0] ? images[0].url : null);
      state.item = item;
      updateVideo(item);
      clearLyrics();
      requestLyrics(item);
    }

    state.duration = item.duration_ms;
    state.progress = data.progress_ms || 0;
    state.syncedAt = performance.now();

    if (Date.now() > state.playLockUntil) {
      state.isPlaying = data.is_playing;
      renderPlaying(state.isPlaying);
    }

    const dev = data.device;
    el.device.textContent = dev ? `Listening on ${dev.name}` : '';
    if (dev && dev.volume_percent != null && Date.now() > state.volumeLockUntil) {
      state.volume = dev.volume_percent;
      renderVolume(dev.volume_percent);
    }
    el.volume.disabled = dev ? dev.supports_volume === false : false;

    el.duration.textContent = fmt(state.duration);
    syncVideo(false);
  }

  function tick() {
    if (state.itemId && state.duration) {
      let pos = state.progress;
      if (state.isPlaying) pos += performance.now() - state.syncedAt;
      pos = Math.min(pos, state.duration);
      el.progressFill.style.width = `${(pos / state.duration) * 100}%`;
      el.elapsed.textContent = fmt(pos);
    } else {
      el.progressFill.style.width = '0%';
      el.elapsed.textContent = '0:00';
      el.duration.textContent = '0:00';
    }
    renderLyrics();
    requestAnimationFrame(tick);
  }


  let pollTimer;
  async function poll() {
    clearTimeout(pollTimer);
    try {
      render(await playerState());
    } catch (err) {
      console.warn(err);
    }
    pollTimer = setTimeout(poll, document.hidden ? POLL_HIDDEN_MS : POLL_MS);
  }

  const repollSoon = (ms = 450) => { clearTimeout(pollTimer); pollTimer = setTimeout(poll, ms); };
  window.pollNow = () => repollSoon(0);


  async function togglePlay() {
    const next = !state.isPlaying;

    if (state.isPlaying) state.progress += performance.now() - state.syncedAt;
    state.syncedAt = performance.now();
    state.isPlaying = next;
    state.playLockUntil = Date.now() + 1500;
    renderPlaying(next);
    syncVideo(false);
    try {
      await control(next ? 'play' : 'pause');
    } catch (err) {
      if (isNetworkFailure(err)) {
        state.playLockUntil = 0;
      } else {
        state.isPlaying = !next;
        state.playLockUntil = 0;
        renderPlaying(!next);
        toast(explain(err));
      }
    }
    repollSoon(700);
  }

  async function skip(dir) {
    try {
      await control(dir > 0 ? 'next' : 'previous');
    } catch (err) {
      if (!isNetworkFailure(err)) toast(explain(err));
    }
    repollSoon(500);
  }

  let volTimer;
  function setVolume(v, immediate = false) {
    v = Math.round(Math.max(0, Math.min(100, v)));
    renderVolume(v);
    state.volume = v;
    state.volumeLockUntil = Date.now() + 2500;
    clearTimeout(volTimer);
    volTimer = setTimeout(async () => {
      try {
        await control('volume', { v });
      } catch (err) {
        if (!isNetworkFailure(err)) toast(explain(err));
      }
    }, immediate ? 0 : 180);
  }

  async function seek(ratio) {
    if (!state.duration) return;
    const pos = Math.round(state.duration * ratio);
    state.progress = pos;
    state.syncedAt = performance.now();
    try {
      await control('seek', { ms: pos });
    } catch (err) {
      if (!isNetworkFailure(err)) toast(explain(err));
    }
    repollSoon(600);
  }

  el.playBtn.addEventListener('click', togglePlay);
  el.nextBtn.addEventListener('click', () => skip(1));
  el.prevBtn.addEventListener('click', () => skip(-1));
  el.volume.addEventListener('input', () => setVolume(+el.volume.value));
  el.muteBtn.addEventListener('click', () => {
    if (state.volume > 0) { state.lastVolume = state.volume; setVolume(0, true); }
    else setVolume(state.lastVolume || 50, true);
  });
  el.progressTrack.addEventListener('click', (e) => {
    const r = el.progressTrack.getBoundingClientRect();
    seek((e.clientX - r.left) / r.width);
  });


  window.addEventListener('wheel', (e) => {
    if (body.classList.contains('is-empty') || state.volume == null) return;
    setVolume(state.volume + (e.deltaY < 0 ? 4 : -4));
  }, { passive: true });

  window.addEventListener('keydown', (e) => {
    if (e.code === 'Space' || e.code === 'MediaPlayPause') { e.preventDefault(); togglePlay(); }
    else if (e.code === 'ArrowRight' || e.code === 'MediaTrackNext') skip(1);
    else if (e.code === 'ArrowLeft' || e.code === 'MediaTrackPrevious') skip(-1);
    else if (e.code === 'ArrowUp') setVolume((state.volume ?? 50) + 5);
    else if (e.code === 'ArrowDown') setVolume((state.volume ?? 50) - 5);
  });


  let idleTimer;
  function wake() {
    body.classList.remove('is-idle');
    clearTimeout(idleTimer);
    idleTimer = setTimeout(() => { if (settings.idleFade !== 'off') body.classList.add('is-idle'); }, 4000);
  }
  ['mousemove', 'mousedown', 'wheel', 'keydown'].forEach((ev) => window.addEventListener(ev, wake, { passive: true }));

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) return;
    if (customBg.current && customBg.current.tagName === 'VIDEO') customBg.current.play().catch(() => {});
    video.stuckSince = 0;
    video.lastTime = -1;
    repollSoon(0);
  });


  function updateClock() {
    const now = new Date();
    const h24 = settings.clock24 === 'on';
    el.clockTime.textContent = now
      .toLocaleTimeString([], { hour: h24 ? '2-digit' : 'numeric', minute: '2-digit', hour12: !h24 })
      .replace(/\s?[AP]M$/i, '');
    el.clockDate.textContent = now.toLocaleDateString([], { weekday: 'long', month: 'long', day: 'numeric' });
  }
  updateClock();
  setInterval(updateClock, 1000);


  loadSettings();
  setInterval(loadSettings, 4000);
  wake();
  requestAnimationFrame(tick);
  poll();
})();
