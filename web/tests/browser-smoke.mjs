import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const url = process.env.SG_EDITOR_URL ?? 'http://127.0.0.1:8777/?token=dev';
const screenshot = process.env.SG_EDITOR_SCREENSHOT;
const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [];
page.on('pageerror', (error) => errors.push(String(error)));
page.on('console', (message) => {
  if (message.type() === 'error') {
    errors.push(message.text());
  }
});

try {
  await page.goto(url, { waitUntil: 'networkidle', timeout: 30000 });
  await page.waitForSelector('.sg-node', { timeout: 15000 });
  assert.ok(await page.locator('.toolbar').count() === 1, 'toolbar is missing');
  assert.ok(await page.locator('.sg-node').count() > 0, 'graph has no nodes');
  assert.ok(await page.locator('.blackboard-row').count() > 0, 'blackboard is empty');

  await page.getByRole('button', { name: '编译' }).click();
  await page.waitForFunction(() => {
    const state = document.querySelector('.compile-state');
    return state?.textContent?.includes('编译成功');
  }, { timeout: 30000 });
  await page.waitForSelector('.preview-stage img', { timeout: 10000 });
  let natural = { width: 0, height: 0 };
  for (let attempt = 0; attempt < 20 && natural.width === 0; ++attempt) {
    await page.waitForTimeout(250);
    natural = await page.locator('.preview-stage img').first().evaluate((img) => ({
      width: img.naturalWidth,
      height: img.naturalHeight,
    })).catch(() => ({ width: 0, height: 0 }));
  }
  assert.ok(natural.width > 0 && natural.height > 0, 'preview image has no pixels');

  await page.locator('.sg-node').first().click();
  await page.waitForFunction(() => {
    const title = document.querySelector('.inspector-heading-title');
    return Boolean(title?.textContent);
  }, { timeout: 5000 });

  if (screenshot) {
    await page.screenshot({ path: screenshot, fullPage: false });
  }
  assert.deepEqual(errors, [], `browser errors: ${errors.join('; ')}`);
  console.log(`web smoke ok: ${url}`);
} finally {
  await browser.close();
}
