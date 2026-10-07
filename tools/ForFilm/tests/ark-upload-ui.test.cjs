// Targeted follow-up for upload button visibility, retry, delayed name read, mobile preview.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { mockBluetooth } = require('./ark-browser.test.cjs');
(async () => {
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ viewport: { width: 1280, height: 960 }, reducedMotion: 'reduce' });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        await page.locator('#scan-button').click();
        await page.waitForFunction(() => ArkDevice.connected());
        await page.locator('[data-page="convert-page"]').click();
        await page.evaluate(() => {
            const image = new Uint8Array(43232);
            image.set([70, 73, 76, 77, 208, 2, 224, 1, 0, 1, 1]);
            window.uploadJob = uploadFilmFileViaBle('ui-test.film', image);
        });
        await page.locator('#ark-upload-cancel').waitFor({ state: 'visible' });
        assert(await page.locator('#transfer-container').isVisible());
        await page.locator('#ark-upload-cancel').click();
        await page.waitForFunction(() => !ArkDevice.busy());
        await page.locator('#ark-upload-retry').click();
        await page.locator('#transfer-status').filter({ hasText: '待设备确认保存' }).waitFor();
        assert(await page.locator('#ark-upload-cancel').isHidden());
        assert(await page.locator('#ark-upload-retry').isVisible());
        await page.waitForFunction(() => document.querySelector('#ark-device-name-current').textContent === 'FRAMEFILMARK-测试');
        await page.locator('[data-page="config-page"]').click();
        await page.waitForFunction(() => getComputedStyle(document.querySelector('#ark-system-section')).opacity === '1');
        await page.screenshot({ path: '.output/ark/web-adaptation/settings-desktop.png', fullPage: true });
        await page.setViewportSize({ width: 390, height: 844 });
        await page.locator('[data-page="pass-page"]').click();
        const editor = page.frameLocator('#ark-pass-editor');
        await editor.locator('#codename').fill('博士');
        await editor.locator('#number').fill('RI-001');
        await page.waitForFunction(() => getComputedStyle(document.querySelector('#ark-pass-editor')).opacity === '1');
        await page.screenshot({ path: '.output/ark/web-adaptation/pass-mobile.png', fullPage: true });
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        assert.deepEqual(errors, []);
        console.log('PASS upload progress/cancel/reconnect/retry, deferred name read and visible mobile editor');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
