// Async availability and connection-scoped direct status; simulated BLE only.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { mockBluetooth } = require('./ark-browser.test.cjs');

(async () => {
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ reducedMotion: 'reduce' });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        await page.locator('#scan-button').click();
        await page.locator('#ark-login-enter').click();
        await page.waitForFunction(() => !ArkDevice.busy());
        await page.evaluate(() => {
            const read = FileReader.prototype.readAsDataURL;
            FileReader.prototype.readAsDataURL = function (file) {
                window.finishImageRead = () => read.call(this, file);
            };
        });
        await page.locator('#frameFileInput').setInputFiles({ name: 'pixel.png', mimeType: 'image/png',
            buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=', 'base64') });
        await page.evaluate(() => {
            window.heldOperation = ArkDevice.run('测试异步图片加载', () => new Promise(resolve => { window.finishOperation = resolve; }));
            window.finishImageRead();
        });
        await page.waitForFunction(() => frameOriginalImage !== null);
        assert(await page.locator('#frameUploadBtn').isDisabled(), 'Image completion does not release busy lock');
        await page.evaluate(async () => { finishOperation(); await heldOperation; });
        assert(await page.locator('#frameUploadBtn').isEnabled(), 'Latest availability survives unlock');
        assert(await page.locator('#frameCameraUploadBtn').isDisabled(), 'Unloaded camera remains unavailable');

        await page.locator('[data-page="config-page"]').click();
        await page.evaluate(() => { mockArk.direct = 4; });
        await page.locator('#ark-direct-refresh').click();
        await page.locator('#ark-direct-status').filter({ hasText: '已完成' }).waitFor();
        assert.equal(await page.locator('#ark-direct-progress').evaluate(el => el.value), 100);
        await page.evaluate(() => device.gatt.disconnect());
        await page.locator('#ark-direct-status').filter({ hasText: '已断开' }).waitFor();
        assert.equal(await page.locator('#ark-direct-progress').evaluate(el => el.value), 0);
        await page.locator('[data-page="bluetooth-page"]').click();
        await page.locator('#ark-reconnect-button').click();
        await page.locator('#ark-login-enter').click();
        await page.locator('[data-page="config-page"]').click();
        await page.locator('#ark-direct-status').filter({ hasText: '尚未查询当前设备' }).waitFor();
        assert.equal(await page.locator('#ark-direct-progress').evaluate(el => el.value), 0);
        assert.deepEqual(errors, []);
        console.log('PASS: actual delayed image completion preserves busy lock and availability; direct status resets on disconnect/reconnect.');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
