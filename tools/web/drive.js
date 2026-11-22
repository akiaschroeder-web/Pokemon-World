// Pokémon World web build, testing aid: drive the game in headless Chromium.
// Waits on what the game is doing (task and callback names) instead of fixed times.
//
// usage: node tools/web/drive.js URL table.json outdir 'JSON steps'
//   table.json: python3 tools/web/wasm_where.py index.wasm --table --json > table.json
// steps:
//   ["until", "NameFragment", maxFrames]   wait until a running task or callback2 matches
//   ["press", "KeyZ", times, gapFrames]    press a key (Z=A, X=B, Enter=Start, arrows)
//   ["hold", "ArrowUp", frames]            hold a key down for a while
//   ["pressUntil", "KeyZ", "NameFragment", maxFrames]  keep pressing until it matches
//   ["wait", frames]
//   ["shot", "name"]                       screenshot of the game screen
//   ["call", "Web_SetTrace", 1]            call an exported debugging function (one number argument)
//   ["trace"]                              log callbacks and tasks every frame
//   ["state"]                              print the running tasks and callbacks
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [url, tablePath, out, stepsJson] = process.argv.slice(2);
const table = JSON.parse(fs.readFileSync(tablePath));
const steps = JSON.parse(stepsJson);
fs.mkdirSync(out, { recursive: true });

(async () => {
  const browser = await chromium.launch({ dumpio: !!process.env.DUMPIO, args: process.env.JSFLAGS ? ['--js-flags=' + process.env.JSFLAGS] : [] });
  const page = await browser.newPage({ viewport: { width: 800, height: 700 } });
  page.on('pageerror', (e) => console.log(`[pageerror] ${e.message}\n${e.stack}`));
  page.on('console', (m) => {
    let t = m.text();
    if (t.startsWith('[AgbMain]')) return;
    if (t.startsWith('[t] ')) t = t.replace(/ (\d+)$/, (_, n) => ' ' + (table[n] || n)); // WEB_TRACE lines
    console.log(`[console] ${t}`);
  });
  // Keep a flood of messages (e.g. a failing assert in a loop) from taking the page down:
  // pass on the first 300, then count the rest.
  await page.addInitScript((MAXLOG) => {
    const log = console.log.bind(console), err = console.error.bind(console);
    let n = 0;
    const wrap = (f) => (...a) => { if (++n <= MAXLOG) f(...a); else if (n % 100000 === 0) f(`[drive] ${n} messages`); };
    console.log = wrap(log); console.error = wrap(err); console.warn = wrap(err);
  }, +(process.env.MAXLOG || 300));
  // If the game stops handing frames back (a loop waiting on something that only the
  // V-blank interrupt would change), pause it in the debugger and print where it is.
  const cdp = await page.context().newCDPSession(page);
  await cdp.send('Debugger.enable');
  cdp.on('Debugger.paused', async (ev) => {
    console.log('[drive] paused in:\n' + ev.callFrames.slice(0, 25).map((f) => '  ' + f.functionName).join('\n'));
    await cdp.send('Debugger.resume').catch(() => {});
  });
  const pauseIfHung = () => cdp.send('Debugger.pause').catch((e) => console.log('[drive] pause failed: ' + e.message));
  // TURBO=1: the game runs as fast as it can (keys are then held for exact frame counts).
  const turbo = !!process.env.TURBO;
  if (turbo) await page.addInitScript(() => { window.Module = window.Module || {}; Module.pwTurbo = true; });
  const hold = async (key, frames) => {
    if (turbo) { await page.evaluate(([k, f]) => Module.pwHoldKeys(Module.pwKeyBits[k], f), [key, frames]); await waitFrames(frames); }
    else { await page.keyboard.down(key); await waitFrames(frames); await page.keyboard.up(key); }
  };
  await page.goto(url);

  const timeout = (ms, v) => new Promise((r) => setTimeout(() => r(v), ms));
  const state = () => Promise.race([page.evaluate(() => {
    if (!window.Module || !Module.calledRun || !Module._Web_DebugAddresses) return null;
    const a = Module._Web_DebugAddresses() >> 2;
    const main = HEAPU32[a + 4] >> 2, tasks = HEAPU32[a + 5];
    const t = [];
    for (let i = 0; i < 16; i++) {
      const base = tasks + i * 40;
      if (HEAPU8[base + 4]) t.push(HEAPU32[base >> 2]);
    }
    return { mem: HEAPU8.length >> 20, frame: Module.pwFrameCount || 0, cb1: HEAPU32[main], cb2: HEAPU32[main + 1], tasks: t };
  }), timeout(3000, 'hung')]);
  const describe = (s) => s && s !== 'hung'
    ? `frame ${s.frame} mem ${s.mem}MB cb2=${table[s.cb2] || s.cb2} cb1=${table[s.cb1] || s.cb1} tasks=[${s.tasks.map((f) => table[f] || f).join(', ')}]`
    : String(s);
  const waitFrames = async (n) => {
    const s0 = await state();
    if (!s0 || s0 === 'hung') throw new Error('page not running: ' + describe(s0));
    for (;;) {
      const s = await state();
      if (s === 'hung') { await pauseIfHung(); await timeout(2000); throw new Error('page hung'); }
      if (s.frame >= s0.frame + n) return s;
      await timeout(turbo ? 5 : 30);
    }
  };

  try {
    for (;;) { const s = await state(); if (s && s !== 'hung' && s.frame > 0) break; await timeout(200); }
    for (const st of steps) {
      const [op, a, b, c] = st;
      if (op === 'wait') await waitFrames(a);
      else if (op === 'call') await page.evaluate(([f, x]) => Module['_' + f](x), [a, b]); // e.g. Web_DebugWindows (prints)
      else if (op === 'trace') await page.evaluate((tbl) => { // log callbacks and tasks every frame from now on
        const w = Module.pwWaitForNextFrame;
        Module.pwWaitForNextFrame = function () {
          const a = Module._Web_DebugAddresses() >> 2, main = HEAPU32[a + 4] >> 2, tasks = HEAPU32[a + 5], t = [];
          for (let i = 0; i < 16; i++) if (HEAPU8[tasks + i * 40 + 4]) t.push(tbl[HEAPU32[(tasks + i * 40) >> 2]]);
          console.log(`[trace] ${Module.pwFrameCount} cb2=${tbl[HEAPU32[main + 1]]} cb1=${tbl[HEAPU32[main]]} vb=${tbl[HEAPU32[main + 2]]} state=${HEAPU8[(main << 2) + 0x438] ?? ''} ${t.join(',')}`);
          return w();
        };
      }, table);
      else if (op === 'state') console.log(describe(await state()));
      else if (op === 'shot') {
        await page.locator('#screen').screenshot({ path: path.join(out, `${a}.png`), timeout: 5000 });
        console.log(`shot ${a}: ${describe(await state())}`);
      } else if (op === 'vram') {
        // raw dump: IO regs (0x400), palette (0x400), VRAM (0x18000), OAM (0x400)
        const b64 = await page.evaluate(() => { const a = Module._Web_DebugAddresses() >> 2;
          const parts = [[HEAPU32[a], 0x400], [HEAPU32[a + 1], 0x400], [HEAPU32[a + 2], 0x18000], [HEAPU32[a + 3], 0x400]];
          let s = ''; for (const [ad, n] of parts) { const u = HEAPU8.subarray(ad, ad + n); for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); }
          return btoa(s); });
        fs.writeFileSync(path.join(out, `${a}.bin`), Buffer.from(b64, 'base64'));
        console.log(`vram ${a}: ${describe(await state())}`);
      } else if (op === 'press') {
        for (let i = 0; i < (b || 1); i++) {
          await hold(a, 3); await waitFrames(c || 8);
        }
      } else if (op === 'hold') {
        await hold(a, b || 16); await waitFrames(4);
      } else if (op === 'chord') {
        // several keys at once, e.g. ["chord", ["KeyS", "Enter"], 6] = R + START (the debug menu)
        const n = b || 6;
        if (turbo) {
          await page.evaluate(([ks, f]) => Module.pwHoldKeys(ks.reduce((m, k) => m | Module.pwKeyBits[k], 0), f), [a, n]);
        } else {
          for (const k of a) await page.keyboard.down(k);
        }
        await waitFrames(n);
        if (!turbo) for (const k of a) await page.keyboard.up(k);
        await waitFrames(4);
      } else if (op === 'pressUntil') {
        // press `a` every 20 frames until a task/callback matching `b` runs (at most c frames)
        const max = c || 3000;
        const s0 = await state();
        for (;;) {
          const s = await state();
          const names = [table[s.cb2] || '', ...s.tasks.map((f) => table[f] || '')];
          if (names.some((n) => n.includes(b))) break;
          if (s.frame > s0.frame + max) { console.log(`pressUntil ${b}: gave up at ${describe(s)}`); break; }
          await hold(a, 3); await waitFrames(17);
        }
      } else if (op === 'until') {
        const max = b || 3000;
        const s0 = await state();
        let s;
        for (;;) {
          s = await state();
          if (s === 'hung') throw new Error('page hung');
          const names = [table[s.cb2] || '', ...s.tasks.map((f) => table[f] || '')];
          if (names.some((n) => n.includes(a))) break;
          if (s.frame > s0.frame + max) { console.log(`until ${a}: gave up at ${describe(s)}`); break; }
          await timeout(30);
        }
      }
    }
  } catch (e) {
    console.log('[drive] ' + e.message);
    await page.locator('#screen').screenshot({ path: path.join(out, 'error.png'), timeout: 5000 }).catch(() => {});
  }
  process.exit(0);
})();
