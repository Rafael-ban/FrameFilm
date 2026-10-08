// Ark presentation only: never initiates a device operation.
(function () {
    'use strict';
    var PREF_KEY = 'framefilm.arkTheme';
    var enabled = true;
    var confirmed = false;
    var welcomed = false;
    var dialog = null;
    var cleanup = null;
    function connected() {
        return confirmed && typeof currentDeviceType !== 'undefined' && currentDeviceType === 'FRAMEFILMARK' &&
            window.ArkDevice && window.ArkDevice.connected();
    }
    function active() { return enabled && connected(); }
    function readPreference() {
        try { var value = localStorage.getItem(PREF_KEY); return value === null || value === '1'; }
        catch (e) { return true; }
    }
    function closeLogin(restoreFocus) {
        if (cleanup) cleanup(restoreFocus !== false);
    }
    function showLogin() {
        if (!active() || dialog) return;
        var previousFocus = document.activeElement;
        var scene = document.createElement('dialog');
        var disposed = false;
        var timers = [];
        var listeners = [];
        var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
        dialog = scene;
        scene.id = 'ark-login'; scene.className = 'ark-login is-intro'; scene.tabIndex = -1;
        scene.setAttribute('aria-labelledby', 'ark-login-title');
        scene.setAttribute('aria-describedby', 'ark-login-description');
        scene.innerHTML = '<div class="ark-login-plane" aria-hidden="true"></div>' +
            '<header class="ark-login-top"><span>RHODES ISLAND</span><span>FRAMEFILM · ARK TERMINAL</span></header>' +
            '<button type="button" class="ark-login-skip" data-ark-local>跳过 / ESC</button>' +
            '<main class="ark-login-main"><div class="ark-login-emblem"><svg class="ark-login-wire" viewBox="0 0 200 180" aria-hidden="true"><path d="M100 8 185 52 185 128 100 172 15 128 15 52Z M100 8 100 172 M15 52 100 96 185 52 M15 128 100 96 185 128"/></svg>' +
            '<img class="ark-login-logo" src="../ui-assets/boot_logo_source.svg" alt="罗德岛" width="1529" height="1299"></div>' +
            '<div class="ark-login-wordmark"><h1 id="ark-login-title">明日方舟</h1><p>ARKNIGHTS</p></div>' +
            '<div class="ark-login-rule" aria-hidden="true"></div>' +
            '<button id="ark-login-enter" class="ark-login-enter" type="button" data-ark-local><span>开始唤醒</span><small>TOUCH TO START</small></button>' +
            '<p id="ark-login-description">进入 Ark 工作台</p></main>' +
            '<div class="ark-login-terminal" aria-hidden="true"><svg viewBox="0 0 200 180"><path d="M100 8 185 52 185 128 100 172 15 128 15 52Z M100 8 100 172 M15 52 100 96 185 52 M15 128 100 96 185 128"/></svg><span>TERMINAL / ACCESS</span></div>' +
            '<footer class="ark-login-bottom"><div class="ark-login-readiness" role="status"><span class="ark-login-resource">正在显现终端标识</span><span class="ark-login-resource-code">UI INTRO</span></div>' +
            '<div class="ark-login-track" aria-hidden="true"><i></i></div><div class="ark-login-meta"><span class="ark-login-device">ARK / 已连接</span><span>FORFILM / TERMINAL INTERFACE</span></div>' +
            '<p class="ark-login-credit">明日方舟主题界面 · 非官方作品</p></footer>';
        function later(fn, ms) {
            var timer = setTimeout(function () { if (!disposed && dialog === scene) fn(); }, ms);
            timers.push(timer);
        }
        function listen(target, name, fn) {
            target.addEventListener(name, fn);
            listeners.push(function () { target.removeEventListener(name, fn); });
        }
        function dispose(restore) {
            if (disposed) return;
            disposed = true;
            timers.forEach(clearTimeout);
            listeners.forEach(function (remove) { remove(); });
            scene.getAnimations({ subtree: true }).forEach(function (animation) { animation.cancel(); });
            if (scene.open) scene.close();
            scene.remove();
            if (dialog !== scene) return;
            dialog = null; cleanup = null;
            document.body.classList.remove('ark-login-open');
            if (restore && previousFocus && previousFocus.isConnected) previousFocus.focus();
        }
        cleanup = dispose;
        function settle() {
            if (disposed || scene.classList.contains('is-entering')) return;
            scene.classList.remove('is-intro'); scene.classList.add('is-ready');
            scene.querySelector('.ark-login-resource').textContent = '界面已就绪';
            scene.querySelector('.ark-login-resource-code').textContent = 'READY';
        }
        function fadeOut() {
            if (disposed || scene.classList.contains('is-leaving')) return;
            scene.classList.add('is-leaving');
            later(function () { dispose(true); }, 280);
        }
        function enter() {
            if (!active()) { dispose(false); return; }
            if (scene.classList.contains('is-entering')) return;
            if (reduced) { dispose(true); return; }
            scene.classList.remove('is-intro'); scene.classList.add('is-entering');
            scene.querySelector('.ark-login-resource').textContent = '正在进入终端';
            scene.querySelector('.ark-login-resource-code').textContent = 'UI TRANSITION';
            listen(scene.querySelector('.ark-login-terminal'), 'animationend', function (event) {
                if (event.animationName === 'ark-terminal-access') fadeOut();
            });
            later(fadeOut, 1050);
        }
        listen(scene.querySelector('.ark-login-skip'), 'click', function () { dispose(true); });
        listen(scene.querySelector('#ark-login-enter'), 'click', enter);
        listen(scene, 'cancel', function (event) { event.preventDefault(); dispose(true); });
        listen(scene, 'close', function () { dispose(true); });
        listen(scene, 'keydown', function (event) {
            if (event.key === 'Enter' && event.target === scene) { event.preventDefault(); enter(); }
        });
        var logo = scene.querySelector('.ark-login-logo');
        listen(logo, 'error', function () { logo.hidden = true; });
        document.body.appendChild(scene);
        document.body.classList.add('ark-login-open');
        scene.showModal(); scene.focus();
        if (reduced) settle(); else later(settle, 1250);
    }
    function arkThemeSync() {
        if (!window.ArkDevice || !window.ArkDevice.connected()) { confirmed = false; welcomed = false; }
        var isActive = active();
        var changed = document.body.classList.contains('theme-ark') !== isActive;
        var brand = document.querySelector('.ark-brand');
        if (isActive && !brand) {
            var head = document.querySelector('.app-head');
            if (head) {
                brand = document.createElement('div'); brand.className = 'ark-brand';
                brand.innerHTML = '<img src="../ui-assets/boot_logo_source.svg" alt="罗德岛"><span>RHODES ISLAND<small>FRAMEFILM / ARK TERMINAL</small></span>';
                head.insertBefore(brand, head.firstChild);
            }
        }
        document.body.classList.toggle('theme-ark', isActive);
        if (!isActive) { if (brand) brand.remove(); closeLogin(false); }
        if (changed) window.dispatchEvent(new CustomEvent('ark-theme-changed', { detail: { active: isActive } }));
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.checked = enabled;
        var hint = document.getElementById('ark-theme-hint');
        if (hint) hint.textContent = enabled ? '连接 FRAMEFILMARK 成功后自动使用明日方舟主题。' : '已关闭：使用默认 ForFilm 主题。';
        var replay = document.getElementById('ark-login-replay');
        if (replay) replay.hidden = !isActive;
        if (isActive && !welcomed) { welcomed = true; showLogin(); }
    }
    // Only the transport's post-notification state event confirms a connection.
    window.addEventListener('ark-device-state', function (event) {
        var next = !!(event.detail && event.detail.connected && window.ArkDevice && window.ArkDevice.connected());
        if (!next) welcomed = false;
        confirmed = next;
        if (document.body) arkThemeSync();
    });
    function init() {
        enabled = readPreference();
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.addEventListener('change', function () {
            enabled = !!sw.checked;
            try { localStorage.setItem(PREF_KEY, enabled ? '1' : '0'); } catch (e) { /* Session preference still works. */ }
            arkThemeSync();
        });
        var card = document.getElementById('theme-settings-card');
        if (card) {
            var replay = document.createElement('button'); replay.type = 'button'; replay.id = 'ark-login-replay';
            replay.className = 'secondary-button'; replay.dataset.arkLocal = ''; replay.textContent = '重看启动界面';
            replay.addEventListener('click', showLogin); card.appendChild(replay);
        }
        arkThemeSync();
    }
    window.arkThemeSync = arkThemeSync;
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
