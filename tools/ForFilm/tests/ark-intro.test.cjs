// Connection-gated intro and real motion. All Bluetooth traffic stays in the fixture.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { mockBluetooth } = require('./ark-browser.test.cjs');

async function controlNotifications() {
    const request = navigator.bluetooth.requestDevice;
    navigator.bluetooth.requestDevice = async () => {
        const peripheral = await request();
        const service = await peripheral.gatt.getPrimaryService();
        const characteristic = await service.getCharacteristic();
        characteristic.startNotifications = async () => {
            if (mockArk.failNotifications) throw new Error('Simulated notification failure');
            if (mockArk.holdNotifications) await new Promise(resolve => { mockArk.releaseNotifications = resolve; });
            return characteristic;
        };
        return peripheral;
    };
}

(async () => {
    const output = path.resolve('.output/ark/connected-intro');
    fs.mkdirSync(output, { recursive: true });
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    const context = await browser.newContext({ viewport: { width: 1100, height: 800 }, reducedMotion: 'no-preference' });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    try {
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.addInitScript(controlNotifications);
        await page.goto('http://127.0.0.1:8768/ForFilm/?theme=ark', { waitUntil: 'networkidle' });
        assert.equal(await page.locator('body.theme-ark').count(), 0, 'Old preview URL cannot enable theme');
        await page.evaluate(() => setDeviceType('FRAMEFILMARK'));
        assert.equal(await page.locator('body.theme-ark').count(), 0, 'Device type alone is insufficient');
        await page.evaluate(() => { mockArk.failNotifications = true; });
        await page.locator('#scan-button').click();
        await page.locator('#connection-status').filter({ hasText: '连接失败' }).waitFor();
        assert.equal(await page.locator('body.theme-ark').count(), 0);
        assert.equal(await page.locator('#ark-login').count(), 0);

        await page.evaluate(() => { mockArk.failNotifications = false; mockArk.holdNotifications = true; });
        await page.locator('#scan-button').click();
        await page.waitForFunction(() => !!mockArk.releaseNotifications);
        assert.equal(await page.locator('body.theme-ark').count(), 0, 'Wait for notifications to be ready');
        await page.evaluate(() => { mockArk.holdNotifications = false; mockArk.releaseNotifications(); });
        await page.locator('#ark-login.is-intro').waitFor();
        await page.waitForFunction(() => document.querySelector('.ark-login-logo').getAnimations().some(a => a.currentTime > 100));
        await page.screenshot({ path: path.join(output, 'intro-motion.png') });
        await page.locator('#ark-login.is-ready').waitFor();
        const logo = await page.locator('.ark-login-logo').evaluate(el => {
            const style = getComputedStyle(el); return { filter: style.filter, opacity: style.opacity, blend: style.mixBlendMode };
        });
        assert.deepEqual(logo, { filter: 'brightness(0)', opacity: '1', blend: 'normal' });
        await page.screenshot({ path: path.join(output, 'connected-login.png') });
        await page.locator('#ark-login-enter').click();
        await page.locator('#ark-login.is-entering').waitFor();
        await page.waitForFunction(() => document.querySelector('.ark-login-terminal svg').getAnimations().some(a => a.currentTime > 180));
        await page.screenshot({ path: path.join(output, 'terminal-motion.png') });
        await page.evaluate(() => device.gatt.disconnect());
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        assert.equal(await page.locator('body.theme-ark').count(), 0);
        // Stay past the interrupted transition's completion to expose stale callbacks.
        await page.waitForTimeout(1400);
        assert.equal(await page.locator('#ark-login, body.theme-ark').count(), 0);

        await page.locator('#ark-reconnect-button').click();
        await page.locator('#ark-login.is-intro').waitFor();
        await page.evaluate(() => { window.testScene = document.querySelector('#ark-login'); });
        await page.evaluate(() => ArkDevice.run('模拟状态查询', ctx => ctx.request(0x42, new Uint8Array())));
        assert(await page.evaluate(() => window.testScene === document.querySelector('#ark-login')));
        await page.locator('#ark-login.is-ready').waitFor();
        await page.locator('#ark-login-enter').click();
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        assert.equal(await page.locator('body.theme-ark').count(), 1, 'Animation finishes in workbench');
        await page.locator('[data-page="config-page"]').click();
        await page.locator('label.switch:has(#ark-theme-switch)').click();
        await page.locator('label.switch:has(#ark-theme-switch)').click();
        assert.equal(await page.locator('#ark-login').count(), 0, 'Preference toggle does not replay same connection');
        await page.setViewportSize({ width: 390, height: 844 });
        await page.locator('#ark-login-replay').click();
        await page.locator('#ark-login.is-ready').waitFor();
        await page.screenshot({ path: path.join(output, 'connected-mobile.png') });
        await page.keyboard.press('Escape');
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        await page.emulateMedia({ reducedMotion: 'reduce' });
        await page.locator('#ark-login-replay').click();
        await page.locator('#ark-login.is-ready').waitFor();
        assert.equal(await page.locator('#ark-login').evaluate(el => el.getAnimations({ subtree: true }).length), 0);
        await page.locator('#ark-login-enter').click();
        await page.locator('#ark-login').waitFor({ state: 'detached' });
        assert.deepEqual(errors, []);
        console.log('PASS: offline/type/failure gates, notification readiness, visible motion, monochrome logo, interrupt cleanup, reconnect, duplicate states, toggle, mobile, Esc and reduced motion.');
    } finally {
        await context.close();
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
