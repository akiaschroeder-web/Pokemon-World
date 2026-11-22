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
//   ["call", "Web_DebugWindows"]           call an exported debugging function
//   ["state"]                              print the running tasks and callbacks
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [url, tablePath, out, stepsJson] = process.argv.slice(2);
const table = JSON.parse(fs.readFileSync(tablePath));
const steps = JSON.parse(stepsJson);
fs.mkdirSync(out, { recursive: true });

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 700 } });
  page.on('pageerror', (e) => console.log(`[pageerror] ${e.message}\n${e.stack}`));
  page.on('console', (m) => { if (!m.text().startsWith('[AgbMain]')) console.log(`[console] ${m.text()}`); });
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
    return { frame: Module.pwFrameCount || 0, cb1: HEAPU32[main], cb2: HEAPU32[main + 1], tasks: t };
  }), timeout(3000, 'hung')]);
  const describe = (s) => s && s !== 'hung'
    ? `frame ${s.frame} cb2=${table[s.cb2] || s.cb2} cb1=${table[s.cb1] || s.cb1} tasks=[${s.tasks.map((f) => table[f] || f).join(', ')}]`
    : String(s);
  const waitFrames = async (n) => {
    const s0 = await state();
    if (!s0 || s0 === 'hung') throw new Error('page not running: ' + describe(s0));
    for (;;) {
      const s = await state();
      if (s === 'hung') throw new Error('page hung');
      if (s.frame >= s0.frame + n) return s;
      await timeout(30);
    }
  };

  try {
    for (;;) { const s = await state(); if (s && s !== 'hung' && s.frame > 0) break; await timeout(200); }
    for (const st of steps) {
      const [op, a, b, c] = st;
      if (op === 'wait') await waitFrames(a);
      else if (op === 'call') await page.evaluate((f) => Module['_' + f](), a); // e.g. Web_DebugWindows (prints)
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
          await page.keyboard.down(a); await waitFrames(3); await page.keyboard.up(a); await waitFrames(c || 8);
        }
      } else if (op === 'hold') {
        await page.keyboard.down(a); await waitFrames(b || 16); await page.keyboard.up(a); await waitFrames(4);
      } else if (op === 'pressUntil') {
        // press `a` every 20 frames until a task/callback matching `b` runs (at most c frames)
        const max = c || 3000;
        const s0 = await state();
        for (;;) {
          const s = await state();
          const names = [table[s.cb2] || '', ...s.tasks.map((f) => table[f] || '')];
          if (names.some((n) => n.includes(b))) break;
          if (s.frame > s0.frame + max) { console.log(`pressUntil ${b}: gave up at ${describe(s)}`); break; }
          await page.keyboard.down(a); await waitFrames(3); await page.keyboard.up(a); await waitFrames(17);
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
