// Theme coverage across ForFilm pages, with simulated BLE and no device writes.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { mockBluetooth } = require('./ark-browser.test.cjs');

(async () => {
    const output = path.resolve('.output/ark/all-pages');
    fs.mkdirSync(output, { recursive: true });
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, reducedMotion: 'reduce' });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        await page.locator('#scan-button').click();
        await page.locator('#ark-login-enter').click();
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        // Recheck only changed states/layout after the first pass has covered the pages.
        for (const width of (process.argv.includes('--states-only') ? [] : [1440, 390])) {
            await page.setViewportSize({ width, height: width === 390 ? 844 : 1000 });
            for (const id of ['frame-page', 'convert-page', 'anim-page', 'config-page']) {
                await page.locator(`[data-page="${id}"]`).click();
                const cardStyles = await page.locator(`#${id} .card:visible`).evaluateAll(els => els.map(el => {
                    const s = getComputedStyle(el); return [s.backgroundColor, s.borderRadius];
                }));
                assert(cardStyles.every(([bg, radius]) => bg === 'rgb(34, 38, 41)' && radius === '0px'), id);
                assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${id} overflow at ${width}`);
                await page.screenshot({ path: path.join(output, `${id}-${width}.png`), fullPage: true });
                if (id === 'frame-page') {
                    for (const kind of ['upload', 'camera', 'quote', 'batch']) {
                        await page.locator(`[data-frame-tab="frame-${kind}"]`).click();
                        await page.locator(`#frame-${kind}.active`).waitFor();
                        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `Frame ${kind} overflow`);
                        await page.screenshot({ path: path.join(output, `frame-${kind}-${width}.png`), fullPage: true });
                        await page.locator('#frameDetailBack').click();
                    }
                }
            }
        }
        await page.setViewportSize({ width: 1440, height: 1000 });
        for (const id of ['convert-page', 'frame-page']) {
            await page.locator(`[data-page="${id}"]`).click();
            if (id === 'frame-page') await page.locator('[data-frame-tab="frame-upload"]').click();
            assert(await page.locator(`#${id}`).evaluate(el => el.getBoundingClientRect().height < 1800), 'No implicit grid rows stretching the page');
            await page.screenshot({ path: path.join(output, `${id}-compact.png`), fullPage: true });
        }
        await page.locator('[data-page="anim-page"]').click();
        await page.locator('#animDrawZoom').click();
        assert.equal(await page.locator('#animDrawZoom').evaluate(el => getComputedStyle(el).color), 'rgb(21, 23, 25)');
        const palette = await page.locator('.anim-swatch').evaluateAll(els => els.map(el => getComputedStyle(el).backgroundColor));
        assert(palette.includes('rgb(255, 255, 255)') && palette.includes('rgb(0, 0, 0)'), 'Actual palette remains unchanged');
        await page.evaluate(() => showMessage('主题检查：操作未完成，请重试。', 'error'));
        const toast = await page.locator('.message.error').evaluate(el => {
            const s = getComputedStyle(el); return [s.backgroundColor, s.color, s.borderRadius, s.borderLeftColor];
        });
        assert.deepEqual(toast, ['rgb(44, 49, 53)', 'rgb(240, 241, 242)', '0px', 'rgb(229, 141, 137)']);
        await page.locator('[data-page="config-page"]').click();
        await page.locator('label.switch:has(#ark-theme-switch)').click();
        assert.equal(await page.locator('body.theme-ark').count(), 0);
        assert.equal(await page.locator('.message.error').evaluate(el => getComputedStyle(el).backgroundColor), 'rgb(244, 67, 54)', 'Toast returns to original theme');
        assert.deepEqual(errors, []);
        console.log('PASS: Frame subpages, Film, animation, settings at desktop/mobile; common card theme, active controls, actual palette, themed toast and original-theme fallback.');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
