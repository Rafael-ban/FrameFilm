// ForFilm · ARK 通行证版皮肤（终末地视觉语言）
// ------------------------------------------------------------
// 默认：ForFilm 打开是现有「贴纸玩具」界面，什么都不做。
// 触发：连接通行证版（currentDeviceType === 'FRAMEFILMARK'）后，
//       播一屏官网同款加载过渡（左缘竖条 + 百分比 + 黄幕擦除），
//       并给 <body> 加 .theme-ark；断开即反向擦除并移除。
// 钩子：utils.js 的 onDeviceTypeChanged() 末尾调用 arkThemeSync()。
// 样式：css/ark-theme.css（全部规则挂在 body.theme-ark 下）
// 设计稿：tools/ui-mockup/ark/index.html

(function () {
    'use strict';

    var ARK_DEVICE = 'FRAMEFILMARK';
    var reduceMotion = !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);

    var state = { active: false, loader: null, timers: [], raf: 0, fonts: false };

    function clearTimers() {
        for (var i = 0; i < state.timers.length; i++) {
            clearTimeout(state.timers[i]);
        }
        state.timers = [];
        if (state.raf) {
            cancelAnimationFrame(state.raf);
            state.raf = 0;
        }
    }

    function later(fn, ms) {
        state.timers.push(setTimeout(fn, ms));
    }

    // ---- 字体：只在 ARK 激活时按需拉取，避免拖累默认皮肤 ----
    function ensureFonts() {
        if (state.fonts) return;
        state.fonts = true;
        if (document.getElementById('ark-theme-fonts')) return;
        var link = document.createElement('link');
        link.id = 'ark-theme-fonts';
        link.rel = 'stylesheet';
        link.href = 'https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@400;500;600;700'
                  + '&family=Barlow:wght@400;500;600;700'
                  + '&family=JetBrains+Mono:wght@400;500;700'
                  + '&family=Noto+Sans+SC:wght@400;500;700&display=swap';
        document.head.appendChild(link);
    }

    // ---- 装饰：品牌行 / ▌段头行 / ◆[短名] 标题 / [ REC ] / 黄横幅 / 遥控器 / KV 标签 / 编号 ----
    // 段头与横幅右侧用的等宽段码
    var PAGE_META = {
        '设备连接': 'LINK',
        'Frame 制作': 'CAPTURE',
        '照片转换': 'PIPELINE',
        '设备设置': 'SYSTEM'
    };
    // 页头标题用短名（对齐设计稿的 [ Frame ] / [ Film ]，而不是原页面全名）
    var PAGE_TITLE = {
        '设备连接': '设备连接',
        'Frame 制作': 'Frame',
        '照片转换': 'Film',
        '设备设置': '设置'
    };

    function syncHeroLabel() {
        var t = document.getElementById('appTitle');
        if (!t) return;
        var raw = (t.textContent || '').trim();
        var key = PAGE_META[raw] || 'ARK';

        var lbl = document.querySelector('.ark-herobar .lbl');
        if (lbl) lbl.textContent = key;

        // ◆ [ 短名 ]（括号单独成元素，保持满字号细灰）
        var ttl = document.querySelector('.ark-title .ttl');
        if (ttl) {
            ttl.textContent = '';
            var b1 = document.createElement('i');
            b1.textContent = '[';
            var b2 = document.createElement('i');
            b2.textContent = ']';
            ttl.appendChild(b1);
            ttl.appendChild(document.createTextNode(PAGE_TITLE[raw] || raw));
            ttl.appendChild(b2);
        }

        // 当前页的黄横幅右侧也换成同一段码
        var active = document.querySelector('.page.active');
        var r = active && active.querySelector('.ark-band .r');
        if (r) r.textContent = key;

        // 桌面页眉的巨型压缩字同样跟随页面（设计稿：// PIPELINE）
        var giantLbl = document.querySelector('.ark-giant span');
        if (giantLbl) giantLbl.textContent = key;
    }

    function decorate() {
        var head = document.querySelector('.app-head');
        var title = document.getElementById('appTitle');

        if (head && title && !head.querySelector('.ark-brandrow')) {
            // ① 品牌行：ENDFIELD 徽标 + FRAMEFILM + ARK 标 …… 电量
            //    （把原电量节点挪进来，ID 不变、click 绑定不受影响）
            var batt = document.getElementById('battery-icon');
            var brandrow = document.createElement('div');
            brandrow.className = 'ark-brandrow';

            var brand = document.createElement('div');
            brand.className = 'ark-brand';
            brand.innerHTML = '<svg class="mk" viewBox="0 0 214 233"><use href="#ark-ef-logo"/></svg>'
                            + '<span class="nm">FRAMEFILM</span>'
                            + '<span class="ark-tag">ARK</span>';
            brandrow.appendChild(brand);

            var sp = document.createElement('span');
            sp.className = 'ark-sp';
            brandrow.appendChild(sp);
            if (batt) brandrow.appendChild(batt);
            head.insertBefore(brandrow, head.firstChild);

            // ② ▌段头行：▌ + 等宽大写标签 + 分隔线 + CMYK 色标（占满整行）
            var herobar = document.createElement('div');
            herobar.className = 'ark-herobar';
            herobar.innerHTML = '<span class="bar"></span><span class="lbl"></span><span class="rule"></span>'
                              + '<span class="cmyk"><i></i><i></i><i></i><i></i></span>';
            head.insertBefore(herobar, brandrow.nextSibling);

            // ③ ◆[ 短名 ] 标题（替换原 #appTitle 的视觉位置；#appTitle 由 CSS 隐藏）
            var arktitle = document.createElement('div');
            arktitle.className = 'ark-title';
            arktitle.innerHTML = '<svg class="dia" viewBox="0 0 24 24"><use href="#ark-ef-dia"/></svg>'
                               + '<span class="ttl"></span>';
            head.insertBefore(arktitle, herobar.nextSibling);

            // ④ [ REC ] 红点标（电量已挪到品牌行，这里直接挂行尾）
            var rec = document.createElement('span');
            rec.className = 'ark-rec';
            rec.innerHTML = '<b></b>[ REC ]';
            head.appendChild(rec);

            syncHeroLabel();

            // 标题随页面切换而变，用 MutationObserver 同步段头标签
            state.titleObserver = new MutationObserver(syncHeroLabel);
            state.titleObserver.observe(title, {
                childList: true, characterData: true, subtree: true
            });
        }

        // 每页一条黄横幅
        if (!document.querySelector('.ark-band')) {
            var pages = document.querySelectorAll('.page');
            for (var i = 0; i < pages.length; i++) {
                var band = document.createElement('div');
                band.className = 'ark-band';
                band.innerHTML = '<svg class="tri" viewBox="0 0 31 28"><use href="#ark-ef-tri-y"/></svg>'
                               + '<span>ARKNIGHTS · ENDFIELD</span>'
                               + '<span class="r">ARK</span>';
                pages[i].insertBefore(band, pages[i].firstChild);
            }
        }

        decorateKv();
        decorateModules();
        decorateGiant();
        decorateSleepTimeline();
        decorateRemote();
        decorateTransfer();
        decoratePreviewReadout();
        // 横幅是后建的，这里再同步一次段头/横幅文案（首屏不点导航也要对）
        syncHeroLabel();
    }

    // 桌面页眉的巨型压缩字（官网 // ENDFIELD 那条；<900px 由 CSS 隐藏）
    function decorateGiant() {
        var head = document.querySelector('.app-head');
        if (!head || head.querySelector('.ark-giant')) return;
        var el = document.createElement('div');
        el.className = 'ark-giant';
        el.innerHTML = '<b>//</b> <span></span>';
        head.appendChild(el);
    }

    // 自动唤醒时长：在滑杆上方补一排 chip 刻度（10m / 24h / 48h），随值高亮
    function decorateSleepTimeline() {
        var wrap = document.querySelector('.wake-slider-wrapper');
        var input = document.getElementById('wake-duration');
        if (!wrap || !input || wrap.querySelector('.ark-tl')) return;

        var tl = document.createElement('div');
        tl.className = 'ark-tl';
        tl.innerHTML = '<span class="ark-tl__mk a" data-v="10">10m</span>'
                     + '<span class="ark-tl__mk b" data-v="1440">24h</span>'
                     + '<span class="ark-tl__mk c" data-v="2880">48h</span>';
        wrap.insertBefore(tl, wrap.firstChild);

        function sync() {
            var v = +input.value;
            var mks = tl.querySelectorAll('.ark-tl__mk');
            for (var i = 0; i < mks.length; i++) {
                mks[i].classList.toggle('on', Math.abs(+mks[i].getAttribute('data-v') - v) < 20);
            }
        }
        input.addEventListener('input', sync);
        input.addEventListener('change', sync);
        sync();
    }

    // 蓝牙遥控：把两行按钮收进一个容器，CSS 用 display:contents 摊平后摆成十字键
    // 遥控键原图标是「上传/下载/对勾/朝下箭头/月亮」——「返回」语义都不对。
    // 这里换一套方向明确的遥控字形；原图元克隆存在节点上，退出主题时还原。
    var REMOTE_GLYPHS = [
        'M12 19V5M6 11l6-6 6 6',                                // 上
        'M12 5v14M6 13l6 6 6-6',                                // 下
        'M5 12.5l4.6 4.6L19 6.6',                               // 确认
        'M14.5 5 7.5 12l7 7',                                   // 返回
        'M20 14.5A8.5 8.5 0 019.5 4a8.5 8.5 0 1010.5 10.5z'     // 休眠
    ];
    var SVG_NS = 'http://www.w3.org/2000/svg';

    function decorateRemote() {
        var sec = document.getElementById('remote-section');
        if (!sec || sec.querySelector('.ark-remote')) return;
        var rows = sec.querySelectorAll('.modes');
        if (rows.length < 2) return;
        var wrap = document.createElement('div');
        wrap.className = 'ark-remote';
        rows[0].parentNode.insertBefore(wrap, rows[0]);
        for (var i = 0; i < rows.length; i++) {
            wrap.appendChild(rows[i]);
        }

        var keys = wrap.querySelectorAll('.mode-button');
        for (var k = 0; k < keys.length && k < REMOTE_GLYPHS.length; k++) {
            var old = keys[k].querySelector('.ic');
            if (!old) continue;
            keys[k].__arkOrigIcon = old.cloneNode(true);
            var next = document.createElementNS(SVG_NS, 'svg');
            next.setAttribute('class', 'ic');
            next.setAttribute('viewBox', '0 0 24 24');
            var p = document.createElementNS(SVG_NS, 'path');
            p.setAttribute('d', REMOTE_GLYPHS[k]);
            next.appendChild(p);
            old.parentNode.replaceChild(next, old);
        }
    }

    // 传输块：在进度条上方补一枚文件名 chip（跟对应输入框联动）
    var TRANSFER_PAIRS = [
        ['frame-transfer-container', 'frameFileName'],
        ['frame-camera-transfer-container', 'frameCameraFileName'],
        ['frame-quote-transfer-container', 'frameQuoteFileName'],
        ['frame-batch-transfer-container', 'batchPrefix'],
        ['transfer-container', 'fileName']
    ];

    function decorateTransfer() {
        for (var i = 0; i < TRANSFER_PAIRS.length; i++) {
            var box = document.getElementById(TRANSFER_PAIRS[i][0]);
            var src = document.getElementById(TRANSFER_PAIRS[i][1]);
            if (!box || box.querySelector('.ark-filechip')) continue;
            var bar = box.querySelector('.progress-bar');
            if (!bar) continue;

            var chip = document.createElement('span');
            chip.className = 'ark-filechip';
            chip.textContent = (src && src.value) ? src.value : 'output.film';
            box.insertBefore(chip, bar);

            if (src) {
                (function (chipEl, srcEl) {
                    srcEl.addEventListener('input', function () {
                        chipEl.textContent = srcEl.value || 'output.film';
                    });
                })(chip, src);
            }
        }
    }

    // 取景框底部的等宽读数（面板 / 排布）
    function decoratePreviewReadout() {
        var frames = document.querySelectorAll('.polaroid-frame');
        if (!frames.length) return;
        var cfg = (typeof getDeviceConfig === 'function') ? getDeviceConfig() : null;
        var size = cfg ? (cfg.screenWidth + ' × ' + cfg.screenHeight) : '720 × 480';
        var layout = cfg ? String(cfg.pixelLayout).toUpperCase() : 'ROTATED-180';

        for (var i = 0; i < frames.length; i++) {
            var card = frames[i].parentNode;
            if (!card || card.querySelector('.ark-readout')) continue;
            var r = document.createElement('div');
            r.className = 'ark-readout';
            r.innerHTML = '<span>PANEL <b>' + size + '</b></span>'
                        + '<span>LAYOUT <b>' + layout + '</b></span>';
            card.appendChild(r);
        }
    }

    // 设备信息 → 键值药丸行：给两个值各补一个黑色标签块（机型 / 面板）
    function decorateKv() {
        var info = document.getElementById('device-type-info');
        if (!info || info.querySelector('.ark-k')) return;
        var badge = document.getElementById('device-type-badge');
        var res = document.getElementById('device-resolution');
        if (badge && badge.parentNode === info) {
            var lb = document.createElement('span');
            lb.className = 'ark-k';
            lb.textContent = '机型';
            info.insertBefore(lb, badge);
        }
        if (res && res.parentNode === info) {
            var lr = document.createElement('span');
            lr.className = 'ark-k';
            lr.textContent = '面板';
            info.insertBefore(lr, res);
        }
    }

    // 内容入口卡右上角的等宽编号
    function decorateModules() {
        var cards = document.querySelectorAll('.mcard');
        for (var i = 0; i < cards.length; i++) {
            if (cards[i].querySelector('.ark-no')) continue;
            var n = document.createElement('span');
            n.className = 'ark-no';
            n.textContent = ('0' + (i + 1)).slice(-2);
            cards[i].appendChild(n);
        }
    }

    // ---- 加载屏 DOM（懒建，默认不进 index.html） ----
    function buildLoader() {
        if (state.loader) return state.loader;
        var el = document.createElement('div');
        el.className = 'ark-loader';
        el.hidden = true;
        el.setAttribute('aria-hidden', 'true');
        el.innerHTML =
            '<div class="ark-loader__bg"></div>' +
            '<div class="ark-loader__bar"><i></i></div>' +
            '<div class="ark-loader__center">' +
                '<svg class="lg" viewBox="0 0 214 233"><use href="#ark-ef-logo"/></svg>' +
                '<div class="nm">FRAMEFILM</div>' +
                '<div class="sub">// ARK EDITION</div>' +
                '<div class="dv"></div>' +
                '<div class="ark-loader__slogrow">' +
                    '<svg class="tris" viewBox="0 0 23 21"><use href="#ark-ef-tris"/></svg>' +
                    '<span>OVER THE FRONTIER / INTO THE FRONT</span>' +
                '</div>' +
            '</div>' +
            '<div class="ark-loader__pct">' +
                '<span class="mk"></span><span class="tick"></span>' +
                '<span class="num"><b>0</b><i>%</i></span>' +
                '<span class="cap">LOADING…</span>' +
            '</div>';
        document.body.appendChild(el);
        state.loader = el;
        return el;
    }

    // 三段加权：主题资源 → 设备信息 → 首帧渲染
    function progressAt(p) {
        if (p < 0.55) return (p / 0.55) * 0.45;
        if (p < 0.80) return 0.45 + ((p - 0.55) / 0.25) * 0.35;
        return 0.80 + ((p - 0.80) / 0.20) * 0.20;
    }

    function runProgress(el, dur, onDone) {
        var bar = el.querySelector('.ark-loader__bar i');
        var num = el.querySelector('.ark-loader__pct .num b');
        if (!bar || !num) { onDone(); return; }

        bar.style.transition = 'none';
        bar.style.height = '0%';
        void bar.offsetHeight;
        bar.style.transition = 'height .26s linear';
        num.textContent = '0';

        var t0 = performance.now();
        function step(now) {
            var p = Math.min(1, (now - t0) / dur);
            var v = Math.round(progressAt(p) * 100);
            bar.style.height = v + '%';
            num.textContent = String(v);
            if (p < 1) {
                state.raf = requestAnimationFrame(step);
            } else {
                onDone();
            }
        }
        state.raf = requestAnimationFrame(step);
    }

    // 退出时把注入的东西全部拆掉，并把被挪走的电量还给 .app-head。
    // 必须做：电量被移进 .ark-brandrow，而那个壳在非主题态是 display:none，
    // 不还原的话其他机型会直接看不到电量。
    function unDecorate() {
        var head = document.querySelector('.app-head');
        var batt = document.getElementById('battery-icon');
        if (head && batt && batt.parentNode && batt.parentNode.classList.contains('ark-brandrow')) {
            head.appendChild(batt);
        }
        if (state.titleObserver) {
            state.titleObserver.disconnect();
            state.titleObserver = null;
        }

        var sels = ['.ark-band', '.ark-brandrow', '.ark-herobar', '.ark-title', '.ark-rec',
                    '.ark-giant', '.ark-tl', '.ark-filechip', '.ark-readout', '.ark-no', '.ark-k'];

        // 遥控器壳：先把它里面的两行 .modes 原样放回，再拆壳
        var remotes = document.querySelectorAll('.ark-remote');
        for (var r = 0; r < remotes.length; r++) {
            var box = remotes[r];
            while (box.firstChild) {
                box.parentNode.insertBefore(box.firstChild, box);
            }
            box.parentNode.removeChild(box);
        }

        for (var i = 0; i < sels.length; i++) {
            var els = document.querySelectorAll(sels[i]);
            for (var j = 0; j < els.length; j++) {
                var el = els[j];
                if (el.parentNode) el.parentNode.removeChild(el);
            }
        }

        // 遥控键：把原图标放回去（换掉的图元克隆挂在按钮节点上）
        var btns = document.querySelectorAll('.mode-button');
        for (var b = 0; b < btns.length; b++) {
            var btn = btns[b];
            if (!btn.__arkOrigIcon) continue;
            var cur = btn.querySelector('.ic');
            if (cur) btn.replaceChild(btn.__arkOrigIcon, cur);
            btn.__arkOrigIcon = null;
        }
    }

    // ---- 进入 ARK 皮肤 ----
    function enter() {
        if (state.active) return;
        state.active = true;
        clearTimers();
        ensureFonts();
        decorate();

        if (reduceMotion) {
            document.body.classList.add('theme-ark');
            return;
        }

        var el = buildLoader();
        el.classList.remove('is-leaving');
        el.hidden = false;
        // 遮屏的同时切底层皮肤，黄幕揭开时已经是新界面
        document.body.classList.add('theme-ark');

        runProgress(el, 1500, function () {
            later(function () { el.classList.add('is-leaving'); }, 260);
            later(function () {
                el.hidden = true;
                el.classList.remove('is-leaving');
            }, 3000);
        });
    }

    // ---- 退出 ARK 皮肤（断开 / 换设备） ----
    function leave() {
        if (!state.active) return;
        state.active = false;
        clearTimers();

        if (reduceMotion) {
            document.body.classList.remove('theme-ark');
            unDecorate();
            return;
        }

        var el = buildLoader();
        el.classList.remove('is-leaving');
        el.hidden = false;
        void el.offsetHeight;
        el.classList.add('is-leaving');

        // 黄幕铺满整屏时再切回原皮肤并拆掉注入物，避免看到中途状态
        later(function () {
            document.body.classList.remove('theme-ark');
            unDecorate();
        }, 1100);
        later(function () {
            el.hidden = true;
            el.classList.remove('is-leaving');
        }, 3200);
    }

    // 由 utils.js 的 onDeviceTypeChanged() 调用
    function arkThemeSync() {
        var isArk = (typeof currentDeviceType !== 'undefined' && currentDeviceType === ARK_DEVICE);
        if (isArk) {
            enter();
        } else {
            leave();
        }
    }

    window.arkThemeSync = arkThemeSync;
})();
