// Pokémon World web build: the page side of the game (loaded before the game code).
// Draws frames, paces the game at 60 frames a second, reads keys and touch buttons,
// and keeps the save in the browser.

var Module = typeof Module !== 'undefined' ? Module : {};

(function () {
  var W = 240, H = 160;
  var FRAME_MS = 1000 / 59.7275; // the GBA's frame rate
  var SAVE_KEY = 'pokemon-world-save-v1';

  // GBA key bits
  var KEY = { A: 1, B: 2, SELECT: 4, START: 8, RIGHT: 16, LEFT: 32, UP: 64, DOWN: 128, R: 256, L: 512 };
  var keyboard = {
    ArrowUp: KEY.UP, ArrowDown: KEY.DOWN, ArrowLeft: KEY.LEFT, ArrowRight: KEY.RIGHT,
    KeyZ: KEY.A, KeyX: KEY.B, Enter: KEY.START, Backspace: KEY.SELECT, ShiftRight: KEY.SELECT,
    KeyA: KEY.L, KeyS: KEY.R,
    KeyW: KEY.UP, KeyK: KEY.A, KeyJ: KEY.B
  };
  var heldKeyboard = 0, heldTouch = 0, heldScript = 0, scriptFrames = 0, sentKeys = -1;

  function sendKeys() {
    var k = heldKeyboard | heldTouch | heldScript;
    if (k !== sentKeys && Module._Web_SetKeys) {
      Module._Web_SetKeys(k);
      sentKeys = k;
    }
  }

  window.addEventListener('keydown', function (e) {
    var bit = keyboard[e.code];
    if (bit) { heldKeyboard |= bit; sendKeys(); e.preventDefault(); }
  });
  window.addEventListener('keyup', function (e) {
    var bit = keyboard[e.code];
    if (bit) { heldKeyboard &= ~bit; sendKeys(); e.preventDefault(); }
  });
  window.addEventListener('blur', function () { heldKeyboard = 0; heldTouch = 0; sendKeys(); });

  // On-screen buttons: any element with data-key="A", "UP", ... (several fingers at once).
  function touchKeys(touches) {
    var k = 0;
    for (var i = 0; i < touches.length; i++) {
      var el = document.elementFromPoint(touches[i].clientX, touches[i].clientY);
      while (el && el !== document.body) {
        if (el.dataset && el.dataset.key) {
          el.dataset.key.split(' ').forEach(function (n) { k |= KEY[n] || 0; });
          break;
        }
        el = el.parentElement;
      }
    }
    return k;
  }
  function onTouch(e) {
    if (!e.target.closest || !e.target.closest('.pad')) return;
    var k = touchKeys(e.touches);
    if (k !== heldTouch && navigator.vibrate && (k & ~heldTouch)) navigator.vibrate(8);
    heldTouch = k;
    document.querySelectorAll('[data-key]').forEach(function (b) {
      var bits = 0;
      b.dataset.key.split(' ').forEach(function (n) { bits |= KEY[n] || 0; });
      b.classList.toggle('down', (heldTouch & bits) === bits && bits !== 0);
    });
    sendKeys();
    e.preventDefault();
  }
  ['touchstart', 'touchmove', 'touchend', 'touchcancel'].forEach(function (t) {
    document.addEventListener(t, onTouch, { passive: false });
  });
  // Mouse, for trying the buttons on a computer.
  document.addEventListener('mousedown', function (e) {
    var b = e.target.closest && e.target.closest('[data-key]');
    if (!b) return;
    var bits = 0;
    b.dataset.key.split(' ').forEach(function (n) { bits |= KEY[n] || 0; });
    heldTouch |= bits; sendKeys();
    var up = function () { heldTouch &= ~bits; sendKeys(); window.removeEventListener('mouseup', up); };
    window.addEventListener('mouseup', up);
  });

  // Frames
  var canvas, ctx, image;
  Module.pwPresentFrame = function (ptr) {
    if (!canvas) {
      canvas = document.getElementById('screen');
      ctx = canvas.getContext('2d');
      image = ctx.createImageData(W, H);
    }
    image.data.set(HEAPU8.subarray(ptr, ptr + W * H * 4));
    ctx.putImageData(image, 0, 0);
    draw3D();
  };

  // Unova (src/platform/web3d.c, platform/web/unova3d.js): the game says which BW zone is on
  // screen, where its camera is and which map sprites it left out of the picture; draw them in 3D.
  var stack3D, on3D = false, ok3D = null;
  var SPRITE_FOOT = 8;        // a map sprite's feet are this far above its bottom edge (GBA pixels)
  function draw3D() {
    if (!Module._Web_Get3DScene || !window.PW3D) return;
    var p = Module._Web_Get3DScene() >> 2, u = HEAPU32, s = HEAP32;
    var zone = u[p];
    if (!stack3D) stack3D = document.getElementById('screen-stack');
    if (!zone) {
      if (on3D) { stack3D.classList.remove('three-d'); on3D = false; }
      return;
    }
    if (ok3D === null) ok3D = PW3D.init(document.getElementById('screen3d'));
    if (!ok3D) return;
    if (!on3D) { stack3D.classList.add('three-d'); on3D = true; }
    var season = new Date().getMonth() % 4;   // like Black/White: January is spring
    PW3D.setScene(zone, u[p + 8], season);
    // screen pixel -> world units in BW's map matrix (the map's tile (0, 0) is map tile (7, 7),
    // and its top-left chunk is at (chunkX, chunkY) in the matrix)
    var camX = s[p + 1] - 7 * 16 + u[p + 9] * 512, camY = s[p + 2] - 7 * 16 + u[p + 10] * 512;
    var count = u[p + 5], sp = u[p + 6], px = u[p + 7];
    var sprites = [];
    for (var i = 0; i < count; i++) {
      var o = sp + 16 * i;
      var x = HEAP16[o >> 1], y = HEAP16[(o >> 1) + 1], w = HEAPU16[(o >> 1) + 2], h = HEAPU16[(o >> 1) + 3];
      var off = u[(o >> 2) + 2], order = HEAPU16[(o >> 1) + 6];
      var wx = camX + x + w / 2, wz = camY + y + h - SPRITE_FOOT;
      sprites.push({ x: wx, z: wz, y: PW3D.groundHeight(wx, wz), w: w, h: h, order: order,
                     image: HEAPU8.subarray(px + 4 * off, px + 4 * (off + w * h)) });
    }
    var tx = camX + 120, tz = camY + 80 + 16;      // the player: centre of the screen
    PW3D.draw([tx, PW3D.groundHeight(tx, tz), tz], sprites, u[p + 3], u[p + 4]);
  }

  // Testing aids (tools/web/drive.js): hold keys for an exact number of frames, and
  // "turbo" (?turbo in the address, or Module.pwTurbo = true): run as fast as possible
  // instead of at the GBA's speed.
  Module.pwKeyBits = keyboard;
  Module.pwHoldKeys = function (bits, frames) {
    heldScript = bits; scriptFrames = frames; sendKeys();
  };
  if (/[?&]turbo\b/.test(location.search)) Module.pwTurbo = true;
  var turboChannel = null, turboResolve = null;

  var nextFrameAt = 0;
  Module.pwFrameCount = 0;
  Module.pwWaitForNextFrame = function () {
    Module.pwFrameCount++;
    if (scriptFrames && --scriptFrames === 0) { heldScript = 0; sendKeys(); }
    if (Module.pwTurbo) {
      if (!turboChannel) {
        turboChannel = new MessageChannel();
        turboChannel.port1.onmessage = function () { var r = turboResolve; turboResolve = null; r && r(); };
      }
      return new Promise(function (resolve) { turboResolve = resolve; turboChannel.port2.postMessage(0); });
    }
    return new Promise(function (resolve) {
      function tick(now) {
        if (document.hidden) { requestAnimationFrame(tick); return; }
        if (!nextFrameAt || now - nextFrameAt > 250) nextFrameAt = now; // after a pause, don't rush
        if (now + 1 >= nextFrameAt) {
          nextFrameAt += FRAME_MS;
          resolve();
        } else {
          requestAnimationFrame(tick);
        }
      }
      requestAnimationFrame(tick);
    });
  };

  // Saves: the 128 KB save chip, kept in localStorage as base64.
  function toBase64(bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i += 0x8000)
      s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
  }
  function fromBase64(b64) {
    var s = atob(b64), out = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
  }
  Module.pwLoadSave = function (ptr, size) {
    try {
      var b64 = localStorage.getItem(SAVE_KEY);
      if (!b64) return;
      var bytes = fromBase64(b64);
      HEAPU8.set(bytes.subarray(0, size), ptr);
    } catch (e) { console.warn('Could not load the save', e); }
  };
  Module.pwStoreSave = function (ptr, size) {
    try {
      localStorage.setItem(SAVE_KEY, toBase64(HEAPU8.subarray(ptr, ptr + size)));
      Module.pwOnSaved && Module.pwOnSaved();
    } catch (e) { console.warn('Could not store the save', e); }
  };
  Module.pwReload = function () { location.reload(); };

  // Save file export/import (also works with .sav files from emulators of the GBA version).
  Module.pwExportSave = function () {
    var b64 = localStorage.getItem(SAVE_KEY);
    if (!b64) { alert('No save yet.'); return; }
    var blob = new Blob([fromBase64(b64)], { type: 'application/octet-stream' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'pokemon-world.sav';
    a.click();
  };
  Module.pwImportSave = function (file) {
    var r = new FileReader();
    r.onload = function () {
      var bytes = new Uint8Array(r.result);
      if (bytes.length < 0x20000) {
        var full = new Uint8Array(0x20000); full.fill(0xFF); full.set(bytes); bytes = full;
      }
      localStorage.setItem(SAVE_KEY, toBase64(bytes.subarray(0, 0x20000)));
      location.reload();
    };
    r.readAsArrayBuffer(file);
  };

  // Sound. Each V-blank the game hands over one frame of sound (stereo float samples at
  // 42048 Hz, src/platform/sound_mixer.c). It goes into a small queue that a Web Audio node
  // (an AudioWorklet, or a ScriptProcessorNode where there is none) plays from, resampled to
  // the device's rate. The queue absorbs the uneven timing of frames; its length is kept near
  // the target by playing very slightly faster or slower. Browsers (Safari above all) only
  // allow sound to start from a touch or key press, so the AudioContext is made then.
  // Without Web Audio the game just runs silently. In turbo mode sound is dropped.
  // The same queue code runs inside the worklet, so it must not use anything from outside.
  function makeAudioQueue(srcRate, dstRate, targetSec) {
    var N = 1 << 15, buf = new Float32Array(N * 2); // 0.78 s of stereo samples
    var written = 0, read = 0, playing = false, target = Math.round(targetSec * srcRate);
    var q = { underruns: 0, dropped: 0 };
    q.push = function (s) { // s: interleaved stereo
      var n = s.length >> 1;
      if (written + n - read > N - 4) { q.dropped++; read = written + n - target; } // far behind: skip ahead
      for (var i = 0; i < n; i++) {
        var j = ((written + i) & (N - 1)) * 2;
        buf[j] = s[i * 2]; buf[j + 1] = s[i * 2 + 1];
      }
      written += n;
    };
    q.buffered = function () { return written - read; };
    q.pull = function (L, R) {
      var n = L.length, i = 0, avail = written - read;
      if (!playing && avail >= target) playing = true;
      if (playing) {
        // drift correction: up to ±0.3% faster or slower
        var adj = (avail - target) / target * 0.003;
        var step = srcRate / dstRate * (1 + Math.max(-0.003, Math.min(0.003, adj)));
        for (; i < n; i++) {
          if (written - read < 2) { playing = false; q.underruns++; break; }
          var k = Math.floor(read), f = read - k, a = (k & (N - 1)) * 2, b = ((k + 1) & (N - 1)) * 2;
          L[i] = buf[a] + (buf[b] - buf[a]) * f;
          R[i] = buf[a + 1] + (buf[b + 1] - buf[a + 1]) * f;
          read += step;
        }
      }
      for (; i < n; i++) { L[i] = 0; R[i] = 0; }
    };
    return q;
  }

  var AUDIO_TARGET_SEC = 0.07; // ~4 frames of sound queued
  var audio = { ctx: null, send: null, mode: 'none', failed: false, info: null, starting: false };
  var audioStats = { frames: 0, sumSq: 0, peak: 0, nonZero: 0, cryFrames: 0, cryRms: 0, ds: 0, cgb: 0 };

  function setUpAudioOutput(ctx) {
    var srcRate = 42048;
    function useScriptProcessor() {
      var q = makeAudioQueue(srcRate, ctx.sampleRate, AUDIO_TARGET_SEC + 4096 / ctx.sampleRate);
      var sp = ctx.createScriptProcessor(2048, 1, 2); // (older WebKit wants an input channel)
      sp.onaudioprocess = function (e) { q.pull(e.outputBuffer.getChannelData(0), e.outputBuffer.getChannelData(1)); };
      sp.connect(ctx.destination);
      audio.sp = sp; // keep it from being collected
      audio.send = function (s) { q.push(s); };
      audio.info = function () { return { buffered: q.buffered(), underruns: q.underruns, dropped: q.dropped }; };
      audio.mode = 'scriptprocessor';
    }
    if (!ctx.audioWorklet || typeof AudioWorkletNode === 'undefined' || !window.Blob || !window.URL) {
      useScriptProcessor();
      return;
    }
    var src = makeAudioQueue.toString() +
      'class PwAudio extends AudioWorkletProcessor {' +
      ' constructor(o) { super(); var p = o.processorOptions, self = this;' +
      '  this.q = makeAudioQueue(p.srcRate, sampleRate, p.target); this.n = 0;' +
      '  this.port.onmessage = function (e) { self.q.push(e.data); }; }' +
      ' process(inputs, outputs) { var o = outputs[0]; this.q.pull(o[0], o[1] || o[0]);' +
      '  if (++this.n % 375 === 0) this.port.postMessage({ buffered: this.q.buffered(), underruns: this.q.underruns, dropped: this.q.dropped });' +
      '  return true; } }' +
      'registerProcessor("pw-audio", PwAudio);';
    var url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
    audio.mode = 'loading';
    ctx.audioWorklet.addModule(url).then(function () {
      var node = new AudioWorkletNode(ctx, 'pw-audio', {
        numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
        processorOptions: { srcRate: srcRate, target: AUDIO_TARGET_SEC }
      });
      var last = { buffered: 0, underruns: 0, dropped: 0 };
      node.port.onmessage = function (e) { last = e.data; };
      node.connect(ctx.destination);
      audio.node = node;
      audio.send = function (s) { node.port.postMessage(s, [s.buffer]); };
      audio.info = function () { return last; };
      audio.mode = 'worklet';
    }).catch(function (e) {
      console.warn('AudioWorklet unavailable, using ScriptProcessorNode', e);
      try { useScriptProcessor(); } catch (e2) { audio.failed = true; audio.mode = 'none'; }
    });
  }

  function startAudio() {
    if (audio.failed) return;
    try {
      if (!audio.ctx) {
        var AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) { audio.failed = true; return; }
        // iPhone: play like a music app, so the ring/silent switch doesn't mute the game (Safari 17+)
        try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch (e) {}
        try { audio.ctx = new AC({ latencyHint: 'interactive' }); } catch (e) { audio.ctx = new AC(); }
        setUpAudioOutput(audio.ctx);
        // older iOS: playing something from the touch unlocks sound
        var b = audio.ctx.createBufferSource();
        b.buffer = audio.ctx.createBuffer(1, 1, 22050);
        b.connect(audio.ctx.destination);
        b.start(0);
      }
      if (audio.ctx.state !== 'running' && audio.ctx.resume) audio.ctx.resume().catch(function () {});
    } catch (e) {
      console.warn('No sound', e);
      audio.failed = true;
    }
  }
  ['touchstart', 'touchend', 'mousedown', 'keydown', 'click'].forEach(function (t) {
    window.addEventListener(t, startAudio, { capture: true, passive: true });
  });
  document.addEventListener('visibilitychange', function () {
    if (!document.hidden && audio.ctx && audio.ctx.state !== 'running') audio.ctx.resume().catch(function () {});
  });

  // Called by the game every V-blank (src/platform/web.c).
  Module.pwQueueAudio = function (ptr, count, rate) {
    var s = HEAPF32.subarray(ptr >> 2, (ptr >> 2) + count * 2);
    if (Module.pwAudioLog || Module.pwAudioCapture) {
      // testing aid (tools/web/drive.js): loudness once a second, and capture of the samples
      var sumSq = 0, peak = 0, nz = 0;
      for (var i = 0; i < s.length; i++) { var v = s[i]; sumSq += v * v; if (v !== 0) nz++; if (v > peak) peak = v; else if (-v > peak) peak = -v; }
      audioStats.frames++; audioStats.sumSq += sumSq; audioStats.nonZero += nz; if (peak > audioStats.peak) audioStats.peak = peak;
      var d = Module._Web_GetAudio() >> 2; // what was playing (src/platform/web.c)
      if (HEAPU32[d + 5] > 0) { audioStats.cryFrames++; audioStats.cryRms = Math.max(audioStats.cryRms, HEAPU32[d + 5] / 1e6); }
      audioStats.ds = Math.max(audioStats.ds, HEAPU32[d + 6]); audioStats.cgb = Math.max(audioStats.cgb, HEAPU32[d + 7]);
      if (Module.pwAudioCapture && Module.pwAudioCapture.length < 3600) Module.pwAudioCapture.push(new Float32Array(s));
      if (Module.pwAudioLog && audioStats.frames >= 60) {
        var rms = Math.sqrt(audioStats.sumSq / (audioStats.frames * count * 2));
        console.log('[audio] frame ' + Module.pwFrameCount + ' rms ' + rms.toFixed(5) + ' peak ' + audioStats.peak.toFixed(4) +
                    ' nonzero ' + audioStats.nonZero + ' chans ' + audioStats.ds + '+' + audioStats.cgb + 'gb' +
                    ' cry ' + audioStats.cryFrames + 'f/' + audioStats.cryRms.toFixed(5) +
                    ' out ' + audio.mode + '/' + (audio.ctx ? audio.ctx.state : 'none'));
        audioStats = { frames: 0, sumSq: 0, peak: 0, nonZero: 0, cryFrames: 0, cryRms: 0, ds: 0, cgb: 0 };
      }
    }
    if (Module.pwTurbo || !audio.send || !audio.ctx || audio.ctx.state !== 'running') return;
    audio.send(new Float32Array(s));
  };
  Module.pwAudioInfo = function () {
    return { mode: audio.mode, state: audio.ctx ? audio.ctx.state : 'none', sampleRate: audio.ctx ? audio.ctx.sampleRate : 0,
             queue: audio.info ? audio.info() : null };
  };

  Module.print = function (t) { console.log(t); };
  Module.printErr = function (t) { console.warn(t); };
})();
