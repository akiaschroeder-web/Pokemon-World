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
  };

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

  Module.print = function (t) { console.log(t); };
  Module.printErr = function (t) { console.warn(t); };
})();
