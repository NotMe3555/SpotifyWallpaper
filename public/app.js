(() => {
  const API = 'https://api.spotify.com/v1';
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
    bgVideo: $('bgVideo'), artVideo: $('artVideo'), vizBars: $('vizBars'), vizRing: $('vizRing'),
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


  const ACCENTS = {
    white: '232 234 240', blue: '130 175 255', green: '110 220 160', pink: '255 145 195',
    orange: '255 175 105', purple: '185 155 255', red: '255 125 125',
  };
  const FLAGS = ['clock', 'date', 'art', 'album', 'progress', 'controls', 'volume', 'device', 'idleFade', 'motion', 'grain', 'vignette'];
  let settings = {};
  let lastArtImg = null;

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
    updateClock();
    updateVideo(state.item);
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


  let token = null;
  let tokenExp = 0;

  class ApiError extends Error {
    constructor(status, message, reason) { super(message); this.status = status; this.reason = reason; }
  }

  async function getToken() {
    if (token && Date.now() < tokenExp - 30_000) return token;
    const res = await fetch('/token', { cache: 'no-store' });
    if (!res.ok) { token = null; throw new ApiError(401, 'not_authenticated'); }
    const data = await res.json();
    token = data.access_token;
    tokenExp = data.expires_at;
    return token;
  }

  async function api(method, path) {
    const t = await getToken();
    const res = await fetch(API + path, { method, headers: { Authorization: `Bearer ${t}` } });
    if (res.status === 401) token = null;
    if (!res.ok) {
      let err = {};
      try { err = (await res.json()).error || {}; } catch {}
      throw new ApiError(res.status, err.message || `HTTP ${res.status}`, err.reason);
    }
    const text = await res.text();
    return text ? JSON.parse(text) : null;
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
    if (err.status === 404 || err.reason === 'NO_ACTIVE_DEVICE') return 'Open Spotify on a device to start listening';
    if (err.reason === 'VOLUME_CONTROL_DISALLOW') return 'This device doesn’t allow volume control';
    if (err.reason === 'PREMIUM_REQUIRED') return 'Playback controls need Spotify Premium';
    if (err.status === 403) return 'That action isn’t allowed right now';
    if (err.status === 429) return 'Spotify is rate limiting — slowing down';
    return 'Couldn’t reach Spotify';
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
  };
  const REVEAL_AFTER_MS = 3500;
  const REVEAL_AGAIN_MS = 1200;
  const FADE_OUT_MS = 400;
  const END_MARGIN_S = 22;
  let ytApi = null;

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
    return length > 0 && (p.getCurrentTime() || 0) > Math.max(length * 0.5, length - END_MARGIN_S);
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
        modestbranding: 1, playsinline: 1, rel: 0, start: Math.floor(start),
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
      startReveal();
      return;
    }
    cutVideo(false);
    if (!video.stuckSince) { video.stuckSince = now; return; }
    const stuckFor = now - video.stuckSince;
    if (stuckFor > 15000) {
      video.stuckSince = 0;
      video.key = null;
      destroyVideo();
      updateVideo(state.item);
    } else if (stuckFor > 2500) {
      try { p.seekTo(position() / 1000, true); p.playVideo(); } catch {}
    }
  }
  setInterval(videoWatchdog, 1500);


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
    if (force || Math.abs(p.getCurrentTime() - target) > 2.5) {
      if (video.shown) cutVideo(true);
      p.seekTo(target, true);
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
    requestAnimationFrame(tick);
  }


  let pollTimer;
  async function poll() {
    clearTimeout(pollTimer);
    let delay = document.hidden ? POLL_HIDDEN_MS : POLL_MS;
    try {
      const data = await api('GET', '/me/player?additional_types=track,episode');
      body.classList.remove('needs-auth');
      render(data);
    } catch (err) {
      if (err.status === 401 && err.message === 'not_authenticated') {
        body.classList.add('needs-auth');
        delay = 3000;
      } else if (err.status === 429) {
        delay = 15000;
      } else {
        console.warn(err);
      }
    }
    pollTimer = setTimeout(poll, delay);
  }

  const repollSoon = (ms = 450) => { clearTimeout(pollTimer); pollTimer = setTimeout(poll, ms); };


  async function togglePlay() {
    const next = !state.isPlaying;

    if (state.isPlaying) state.progress += performance.now() - state.syncedAt;
    state.syncedAt = performance.now();
    state.isPlaying = next;
    state.playLockUntil = Date.now() + 1500;
    renderPlaying(next);
    syncVideo(false);
    try {
      await api('PUT', next ? '/me/player/play' : '/me/player/pause');
    } catch (err) {
      state.isPlaying = !next;
      state.playLockUntil = 0;
      renderPlaying(!next);
      toast(explain(err));
    }
    repollSoon(700);
  }

  async function skip(dir) {
    try {
      await api('POST', dir > 0 ? '/me/player/next' : '/me/player/previous');
    } catch (err) {
      toast(explain(err));
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
        await api('PUT', `/me/player/volume?volume_percent=${v}`);
      } catch (err) {
        toast(explain(err));
      }
    }, immediate ? 0 : 180);
  }

  async function seek(ratio) {
    if (!state.duration) return;
    const pos = Math.round(state.duration * ratio);
    state.progress = pos;
    state.syncedAt = performance.now();
    try {
      await api('PUT', `/me/player/seek?position_ms=${pos}`);
    } catch (err) {
      toast(explain(err));
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
