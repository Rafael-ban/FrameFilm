// ForFilm · Rhodes Island theme. Device identity and connection state stay untouched.
(function () {
    'use strict';
    var PREF_KEY = 'framefilm.arkTheme';
    var preview = new URLSearchParams(window.location.search).get('theme') === 'ark';
    var enabled = true;
    function readPreference() {
        try {
            var value = window.localStorage.getItem(PREF_KEY);
            return value === null || value === '1';
        } catch (e) { return true; }
    }
    function syncThemeSwitchUI() {
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.checked = enabled;
        var hint = document.getElementById('ark-theme-hint');
        if (hint) hint.textContent = preview
            ? (enabled ? '预览模式：罗德岛主题已启用，设备连接状态保持真实。' : '预览模式：已切回默认 ForFilm 主题。')
            : (enabled ? '已启用：连接 FRAMEFILMARK 后自动使用罗德岛主题。' : '已关闭：使用默认 ForFilm 主题。');
    }
    function arkThemeSync() {
        var isArk = typeof currentDeviceType !== 'undefined' && currentDeviceType === 'FRAMEFILMARK';
        var active = enabled && (isArk || preview);
        var changed = document.body.classList.contains('theme-ark') !== active;
        var brand = document.querySelector('.ark-brand');
        if (active && !brand) {
            var head = document.querySelector('.app-head');
            if (head) {
                brand = document.createElement('div');
                brand.className = 'ark-brand';
                brand.innerHTML = '<img src="../ui-assets/boot_logo_mono.png" alt="罗德岛"><span>RHODES ISLAND<small>FRAMEFILM / ARK</small></span>';
                head.insertBefore(brand, head.firstChild);
            }
        }
        document.body.classList.toggle('theme-ark', active);
        if (!active && brand) brand.remove();
        if (changed) window.dispatchEvent(new CustomEvent('ark-theme-changed', { detail: { active: active } }));
        syncThemeSwitchUI();
    }
    function init() {
        // A URL preview is temporary, including its toggle, and never changes saved preferences.
        enabled = preview || readPreference();
        var sw = document.getElementById('ark-theme-switch');
        if (sw) sw.addEventListener('change', function () {
            enabled = !!sw.checked;
            if (!preview) {
                try { window.localStorage.setItem(PREF_KEY, enabled ? '1' : '0'); } catch (e) { /* Session preference still works. */ }
            }
            arkThemeSync();
        });
        arkThemeSync();
    }
    window.arkThemeSync = arkThemeSync;
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();
})();
