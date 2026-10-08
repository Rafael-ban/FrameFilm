// Layout and interaction polish only. The Bluetooth fixture never writes to hardware.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { mockBluetooth } = require('./ark-browser.test.cjs');

(async () => {
    const output = path.resolve('.output/ark/ui-polish');
    fs.mkdirSync(output, { recursive: true });
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        assert.equal(await page.locator('body.theme-ark').count(), 0);
        await page.locator('#scan-button').click();
        await page.locator('#ark-login.is-ready').waitFor();
        for (const [width, height] of [[1440, 1000], [390, 844], [844, 390], [568, 320]]) {
            await page.setViewportSize({ width, height });
            const layout = await page.locator('#ark-login').evaluate(el => {
                const main = el.querySelector('.ark-login-main').getBoundingClientRect();
                const footer = el.querySelector('.ark-login-bottom').getBoundingClientRect();
                const skip = el.querySelector('.ark-login-skip').getBoundingClientRect();
                return { overlap: main.bottom > footer.top + 1, overflow: el.scrollWidth > el.clientWidth, skipHeight: skip.height };
            });
            assert(!layout.overlap, `Login footer overlap at ${width}x${height}`);
            assert(!layout.overflow, `Login horizontal overflow at ${width}x${height}`);
            assert(layout.skipHeight >= 44, 'Skip has a usable touch target');
            await page.screenshot({ path: path.join(output, `login-${width}x${height}.png`) });
        }
        await page.setViewportSize({ width: 1440, height: 1000 });
        await page.locator('#ark-login-enter').click();
        await page.waitForFunction(() => {
            const el = document.querySelector('#ark-login.is-leaving');
            if (!el) return false;
            const opacity = Number(getComputedStyle(el, '::backdrop').opacity);
            return opacity > 0 && opacity < .95;
        });
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        await page.locator('#scan-button').hover();
        await page.waitForTimeout(180); // Color transition duration is 150ms.
        assert(await page.locator('#scan-button').evaluate(el =>
            getComputedStyle(el).color === getComputedStyle(el.querySelector('.ic')).stroke));
        await page.screenshot({ path: path.join(output, 'workbench-desktop.png'), fullPage: true });
        await page.locator('[data-page="pass-page"]').click();
        const editor = page.frameLocator('#ark-pass-editor');
        await editor.locator('body.theme-ark').waitFor();
        await editor.locator('#codename').fill('博士');
        await editor.locator('#import-json').focus();
        const focus = await editor.locator('.file-button').evaluate(el => {
            const s = getComputedStyle(el); return { color: s.outlineColor, width: s.outlineWidth };
        });
        assert.deepEqual(focus, { color: 'rgb(131, 175, 193)', width: '2px' });
        await page.screenshot({ path: path.join(output, 'pass-desktop.png'), fullPage: true });
        await page.setViewportSize({ width: 390, height: 844 });
        assert.equal(await editor.locator('#codename').evaluate(el => getComputedStyle(el).fontSize), '16px');
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        assert(await editor.locator('body').evaluate(el => el.ownerDocument.documentElement.scrollWidth <= innerWidth));
        await page.screenshot({ path: path.join(output, 'pass-mobile.png'), fullPage: true });
        await page.locator('[data-page="config-page"]').click();
        await page.emulateMedia({ reducedMotion: 'reduce' });
        await page.locator('#ark-login-replay').click();
        await page.locator('#ark-login.is-ready').waitFor();
        assert.equal(await page.locator('#ark-login').evaluate(el => el.getAnimations({ subtree: true }).length), 0);
        await page.keyboard.press('Escape');
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        assert.deepEqual(errors, []);
        console.log('PASS: login layout at four sizes, backdrop fade, hover icon contrast, editor focus, mobile input/layout, reduced motion.');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
