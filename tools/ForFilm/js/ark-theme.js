// Ark presentation only: never initiates a device operation.
(function () {
    'use strict';
    var PREF_KEY = 'framefilm.arkTheme';
    var preview = new URLSearchParams(window.location.search).get('theme') === 'ark';
    var enabled = true;
    var welcomed = false;
    var dialog = null;
    var previousFocus = null;
    function readPreference() {
        try { var value = localStorage.getItem(PREF_KEY); return value === null || value === '1'; }
        catch (e) { return true; }
    }
    function syncThemeSwitchUI() {
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.checked = enabled;
        var hint = document.getElementById('ark-theme-hint');
        if (hint) hint.textContent = preview
            ? '临时外观预览 · 不连接设备，也不保存主题偏好。'
            : (enabled ? '连接 FRAMEFILMARK 后自动使用明日方舟主题。' : '已关闭：使用默认 ForFilm 主题。');
        var replay = document.getElementById('ark-login-replay');
        if (replay) replay.hidden = !document.body.classList.contains('theme-ark');
        updateDeviceStatus();
    }
    function updateDeviceStatus() {
        if (!dialog || !dialog.open) return;
        var status = document.getElementById('connection-status');
        dialog.querySelector('.ark-login-device').textContent = preview
            ? 'APPEARANCE PREVIEW / 外观预览'
            : 'DEVICE / ' + (status ? status.textContent.trim() : '设备状态待更新');
    }
    function closeLogin() {
        if (!dialog || !dialog.open) return;
        dialog.close();
    }
    function createLogin() {
        if (dialog) return;
        dialog = document.createElement('dialog');
        dialog.id = 'ark-login';
        dialog.className = 'ark-login';
        dialog.tabIndex = -1;
        dialog.setAttribute('aria-labelledby', 'ark-login-title');
        dialog.setAttribute('aria-describedby', 'ark-login-description');
        dialog.innerHTML = '<div class="ark-login-plane" aria-hidden="true"></div>' +
            '<header class="ark-login-top"><span>RHODES ISLAND</span><span>FRAMEFILM · ARK TERMINAL</span></header>' +
            '<button type="button" class="ark-login-skip" data-ark-local>跳过 / ESC</button>' +
            '<main class="ark-login-main"><img class="ark-login-logo" src="../ui-assets/boot_logo_source.svg" alt="罗德岛" width="1529" height="1299">' +
            '<div class="ark-login-wordmark"><h1 id="ark-login-title">明日方舟</h1><p>ARKNIGHTS</p></div>' +
            '<div class="ark-login-rule" aria-hidden="true"></div>' +
            '<button id="ark-login-enter" class="ark-login-enter" type="button" data-ark-local><span>开始唤醒</span><small>TOUCH TO START</small></button>' +
            '<p id="ark-login-description">进入 Ark 工作台</p></main>' +
            '<footer class="ark-login-bottom"><div class="ark-login-readiness" role="status"><span class="ark-login-resource">正在准备界面资源</span><span class="ark-login-resource-code">UI RESOURCE</span></div>' +
            '<div class="ark-login-track" aria-hidden="true"><i></i></div><div class="ark-login-meta"><span class="ark-login-device"></span><span>FORFILM / TERMINAL INTERFACE</span></div>' +
            '<p class="ark-login-credit">明日方舟主题界面 · 非官方作品</p></footer>';
        document.body.appendChild(dialog);
        dialog.querySelector('.ark-login-skip').addEventListener('click', closeLogin);
        dialog.querySelector('#ark-login-enter').addEventListener('click', closeLogin);
        dialog.addEventListener('close', function () {
            document.body.classList.remove('ark-login-open');
            if (previousFocus && previousFocus.isConnected) previousFocus.focus();
            else { var target = document.querySelector('.nav-item.active, .nav-item, #ark-theme-switch'); if (target) target.focus(); }
        });
        dialog.addEventListener('keydown', function (event) {
            if (event.key === 'Enter' && event.target === dialog) { event.preventDefault(); closeLogin(); }
            if (event.key !== 'Tab') return;
            var first = dialog.querySelector('.ark-login-skip');
            var last = dialog.querySelector('#ark-login-enter');
            if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
            else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
        });
        var status = document.getElementById('connection-status');
        if (status) new MutationObserver(updateDeviceStatus).observe(status, { childList: true, subtree: true, characterData: true });
    }
    function showLogin() {
        createLogin();
        if (dialog.open) return;
        previousFocus = document.activeElement;
        document.body.classList.add('ark-login-open');
        dialog.showModal();
        updateDeviceStatus();
        dialog.focus();
        var logo = dialog.querySelector('.ark-login-logo');
        function ready(failed) {
            dialog.classList.add('is-ready');
            dialog.querySelector('.ark-login-resource').textContent = failed ? '界面已就绪 · 标识资源不可用' : '界面已就绪';
            dialog.querySelector('.ark-login-resource-code').textContent = 'READY';
            if (failed) logo.hidden = true;
        }
        // Resource readiness only; no simulated BLE or initialization percentage.
        var resource = logo.decode ? logo.decode() : new Promise(function (resolve, reject) {
            if (logo.complete) { (logo.naturalWidth ? resolve : reject)(); return; }
            logo.onload = resolve; logo.onerror = reject;
        });
        var timeout;
        Promise.race([resource, new Promise(function (resolve, reject) { timeout = setTimeout(reject, 4000); })])
            .then(function () { clearTimeout(timeout); requestAnimationFrame(function () { ready(false); }); }, function () { clearTimeout(timeout); ready(true); });
    }
    function arkThemeSync() {
        var isArk = typeof currentDeviceType !== 'undefined' && currentDeviceType === 'FRAMEFILMARK';
        var active = enabled && (isArk || preview);
        var changed = document.body.classList.contains('theme-ark') !== active;
        var brand = document.querySelector('.ark-brand');
        if (active && !brand) {
            var head = document.querySelector('.app-head');
            if (head) {
                brand = document.createElement('div'); brand.className = 'ark-brand';
                brand.innerHTML = '<img src="../ui-assets/boot_logo_source.svg" alt="罗德岛"><span>RHODES ISLAND<small>FRAMEFILM / ARK TERMINAL</small></span>';
                head.insertBefore(brand, head.firstChild);
            }
        }
        document.body.classList.toggle('theme-ark', active);
        if (!active) { if (brand) brand.remove(); closeLogin(); }
        if (changed) window.dispatchEvent(new CustomEvent('ark-theme-changed', { detail: { active: active } }));
        syncThemeSwitchUI();
        if (active && !welcomed) { welcomed = true; showLogin(); }
    }
    function init() {
        enabled = preview || readPreference();
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.addEventListener('change', function () {
            enabled = !!sw.checked;
            if (!preview) { try { localStorage.setItem(PREF_KEY, enabled ? '1' : '0'); } catch (e) { /* Session preference still works. */ } }
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
