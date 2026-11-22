// Pokémon World web build: open the game in headless Chromium, press some buttons
// and take screenshots, to check that the build runs.
// usage: node tools/web/smoke_test.js http://localhost:8000/ outdir
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const url = process.argv[2];
const out = process.argv[3] || 'screenshots';
fs.mkdirSync(out, { recursive: true });

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 960, height: 700 } });
  const log = [];
  page.on('console', (m) => log.push(`[${m.type()}] ${m.text()}`));
  page.on('pageerror', (e) => log.push(`[pageerror] ${e.message}`));
  await page.goto(url);

  // If the game hangs the page's main thread, evaluate() never returns; give up after 3 s.
  const frames = async () => Promise.race([
    page.evaluate(() => (window.Module && Module.pwFrameCount) || 0),
    new Promise((r) => setTimeout(() => r(-1), 3000)),
  ]);
  const shot = async (name) => {
    const n = await frames();
    log.push(`--- ${name}: frame ${n}`);
    await page.locator('#screen').screenshot({ path: path.join(out, `${name}.png`), timeout: 5000 });
  };
  const waitFrames = async (count, timeoutMs = 60000) => {
    const start = await frames();
    const t0 = Date.now();
    if (start < 0) throw new Error('the page is not responding (game stuck in a loop?)');
    while (Date.now() - t0 < timeoutMs) {
      const n = await frames();
      if (n < 0) throw new Error('the page stopped responding (game stuck in a loop?)');
      if (n >= start + count) break;
      await new Promise((r) => setTimeout(r, 100));
    }
  };
  const press = async (key, holdFrames = 6) => {
    await page.keyboard.down(key);
    await waitFrames(holdFrames);
    await page.keyboard.up(key);
    await waitFrames(holdFrames);
  };

  try {
    await waitFrames(60, 120000);
    await shot('01-start');
    await waitFrames(400);
    await shot('02-intro');
    await press('Enter');           // skip the intro
    await waitFrames(240);
    await shot('03-title');
    await press('Enter');           // title screen -> main menu
    await waitFrames(120);
    await shot('04-menu');
    for (let i = 0; i < 12; i++) await press('KeyZ', 8);
    await shot('05-after-A');
  } catch (e) {
    log.push(`[smoke test] ${e}`);
  }
  log.push(`frames run: ${await frames().catch(() => '?')}`);
  fs.writeFileSync(path.join(out, 'console.txt'), log.join('\n') + '\n');
  await browser.close();
})();
