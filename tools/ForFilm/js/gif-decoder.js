// 极简 GIF 解码器（零依赖）
// ------------------------------------------------------------
// 用途：ForFilm「动画工坊」把 GIF 拆成逐帧位图，交给 animation.js 量化成多帧 .film。
// 覆盖：GIF87a / GIF89a、全局/局部颜色表、图形控制扩展（延时/透明色/处置方式）、
//       注释与应用扩展（跳过）、隔行扫描、LZW 可变长码。
// 返回：[{ imageData: ImageData(逻辑屏尺寸), delayMs: number }, ...]
//
// 说明：只做"能正确取到每一帧画面"所需的最小实现，不做渐进式解码、不做多帧合并优化。
(function (global) {
    'use strict';

    var MAX_FRAMES_HARD = 512;      // 防御性上限：异常 GIF 不至于把内存吃光

    function GifError(msg) {
        this.name = 'GifError';
        this.message = msg;
    }
    GifError.prototype = Object.create(Error.prototype);

    function readColorTable(bytes, pos, count) {
        var table = new Array(count);
        for (var i = 0; i < count; i++) {
            var o = pos + i * 3;
            table[i] = [bytes[o], bytes[o + 1], bytes[o + 2]];
        }
        return { table: table, next: pos + count * 3 };
    }

    // 子块序列：长度字节 + 数据，直到长度 0；把数据拼成一整块
    function readSubBlocks(bytes, pos) {
        var chunks = [];
        var total = 0;
        while (pos < bytes.length) {
            var size = bytes[pos++];
            if (size === 0) break;
            chunks.push(bytes.subarray(pos, pos + size));
            total += size;
            pos += size;
        }
        var out = new Uint8Array(total);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) {
            out.set(chunks[i], off);
            off += chunks[i].length;
        }
        return { data: out, next: pos };
    }

    // LZW 解码：输出 pixelCount 个颜色索引
    function lzwDecode(minCodeSize, data, pixelCount) {
        var clearCode = 1 << minCodeSize;
        var eoiCode = clearCode + 1;
        var out = new Uint8Array(pixelCount);
        var outPos = 0;

        var dict = null;
        var codeSize = 0;
        var prev = null;

        function resetDict() {
            dict = new Array(clearCode + 2);
            for (var i = 0; i < clearCode; i++) {
                dict[i] = [i];
            }
            dict[clearCode] = null;     // clear
            dict[eoiCode] = null;       // eoi
            codeSize = minCodeSize + 1;
        }
        resetDict();

        var bitBuf = 0;
        var bitCount = 0;
        var dataPos = 0;

        while (outPos < pixelCount) {
            while (bitCount < codeSize) {
                if (dataPos >= data.length) {
                    return out;         // 数据提前结束：返回已解出的部分
                }
                bitBuf |= data[dataPos++] << bitCount;
                bitCount += 8;
            }
            var code = bitBuf & ((1 << codeSize) - 1);
            bitBuf >>= codeSize;
            bitCount -= codeSize;

            if (code === clearCode) {
                resetDict();
                prev = null;
                continue;
            }
            if (code === eoiCode) {
                break;
            }

            var entry;
            if (code < dict.length && dict[code]) {
                entry = dict[code];
            } else if (prev) {
                // KwKwK：当前码尚未入表，等于上一串 + 上一串首字符
                entry = prev.concat(prev[0]);
            } else {
                break;                  // 非法码，容错退出
            }

            for (var i = 0; i < entry.length && outPos < pixelCount; i++) {
                out[outPos++] = entry[i];
            }

            if (prev) {
                dict.push(prev.concat(entry[0]));
                if (dict.length === (1 << codeSize) && codeSize < 12) {
                    codeSize++;
                }
            }
            prev = entry;
        }

        return out;
    }

    // 隔行扫描：把按 8/8/4/2 行距存放的索引重排为逐行
    function deinterlace(indices, width, height) {
        var out = new Uint8Array(indices.length);
        var srcRow = 0;
        var starts = [0, 4, 2, 1];
        var steps = [8, 8, 4, 2];
        for (var pass = 0; pass < 4; pass++) {
            for (var y = starts[pass]; y < height; y += steps[pass]) {
                var from = srcRow * width;
                out.set(indices.subarray(from, from + width), y * width);
                srcRow++;
            }
        }
        return out;
    }

    /**
     * 解码 GIF。
     * @param {ArrayBuffer|Uint8Array} input
     * @returns {Array<{imageData: ImageData, delayMs: number}>}
     */
    function decodeGif(input) {
        var bytes = (input instanceof Uint8Array) ? input : new Uint8Array(input);
        if (bytes.length < 13) {
            throw new GifError('GIF 文件过小');
        }
        var sig = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        if (sig !== 'GIF87a' && sig !== 'GIF89a') {
            throw new GifError('不是有效的 GIF 文件');
        }

        var pos = 6;
        var width = bytes[pos] | (bytes[pos + 1] << 8);
        var height = bytes[pos + 2] | (bytes[pos + 3] << 8);
        var packed = bytes[pos + 4];
        pos += 7;                                   // 宽2 + 高2 + packed1 + 背景色1 + 像素比1

        if (width <= 0 || height <= 0) {
            throw new GifError('GIF 尺寸非法');
        }

        var globalTable = null;
        if (packed & 0x80) {
            var g = readColorTable(bytes, pos, 2 << (packed & 7));
            globalTable = g.table;
            pos = g.next;
        }

        // 合成画布（RGBA，逻辑屏坐标，初始全透明）
        var screen = new Uint8ClampedArray(width * height * 4);
        var frames = [];

        var gce = { disposal: 0, delayMs: 100, transparent: -1 };

        while (pos < bytes.length && frames.length < MAX_FRAMES_HARD) {
            var block = bytes[pos++];

            if (block === 0x3B) {                   // trailer
                break;
            }

            if (block === 0x21) {                   // extension
                var label = bytes[pos++];
                if (label === 0xF9) {               // graphic control
                    var size = bytes[pos];          // pos 指向块长度字节
                    if (size >= 4) {
                        var flags = bytes[pos + 1];
                        var delay = bytes[pos + 2] | (bytes[pos + 3] << 8);
                        var tIndex = bytes[pos + 4];
                        gce = {
                            disposal: (flags >> 2) & 7,
                            delayMs: delay > 0 ? delay * 10 : 100,
                            transparent: (flags & 1) ? tIndex : -1
                        };
                    }
                    pos += 1 + size;                // 跳过 长度字节 + 数据
                    while (pos < bytes.length && bytes[pos] === 0) {
                        pos++;                      // 跳过块终止符
                    }
                } else {
                    var sub = readSubBlocks(bytes, pos);
                    pos = sub.next;
                }
                continue;
            }

            if (block !== 0x2C) {                   // 未知块：按子块跳过来容错
                var skip = readSubBlocks(bytes, pos);
                pos = skip.next;
                continue;
            }

            // ---- image descriptor ----
            var left = bytes[pos] | (bytes[pos + 1] << 8);
            var top = bytes[pos + 2] | (bytes[pos + 3] << 8);
            var iw = bytes[pos + 4] | (bytes[pos + 5] << 8);
            var ih = bytes[pos + 6] | (bytes[pos + 7] << 8);
            var ipacked = bytes[pos + 8];
            pos += 9;

            var localTable = null;
            if (ipacked & 0x80) {
                var l = readColorTable(bytes, pos, 2 << (ipacked & 7));
                localTable = l.table;
                pos = l.next;
            }
            var table = localTable || globalTable;
            if (!table) {
                throw new GifError('GIF 缺少颜色表');
            }

            var interlaced = !!(ipacked & 0x40);
            var minCodeSize = bytes[pos++];
            var lzw = readSubBlocks(bytes, pos);
            pos = lzw.next;

            var indices = lzwDecode(minCodeSize, lzw.data, iw * ih);
            if (interlaced) {
                indices = deinterlace(indices, iw, ih);
            }

            // 处置方式 3 需要"本帧绘制前"的整屏快照
            var before = (gce.disposal === 3) ? screen.slice() : null;

            // 逐像素合成到整屏
            for (var y = 0; y < ih; y++) {
                var sy = top + y;
                if (sy < 0 || sy >= height) {
                    continue;
                }
                for (var x = 0; x < iw; x++) {
                    var sx = left + x;
                    if (sx < 0 || sx >= width) {
                        continue;
                    }
                    var idx = indices[y * iw + x];
                    if (idx === gce.transparent) {
                        continue;               // 透明像素不覆盖
                    }
                    var color = table[idx];
                    if (!color) {
                        continue;
                    }
                    var o = (sy * width + sx) * 4;
                    screen[o] = color[0];
                    screen[o + 1] = color[1];
                    screen[o + 2] = color[2];
                    screen[o + 3] = 255;
                }
            }

            // 快照当前帧（复制一份，避免后续处置改动污染）
            var snapshot = new ImageData(new Uint8ClampedArray(screen), width, height);
            frames.push({ imageData: snapshot, delayMs: gce.delayMs });

            // 处置：为下一帧准备画布
            if (gce.disposal === 2) {
                var clearRect = { l: left, t: top, w: iw, h: ih };
                for (var ry = clearRect.t; ry < clearRect.t + clearRect.h; ry++) {
                    if (ry < 0 || ry >= height) continue;
                    for (var rx = clearRect.l; rx < clearRect.l + clearRect.w; rx++) {
                        if (rx < 0 || rx >= width) continue;
                        var co = (ry * width + rx) * 4;
                        screen[co] = 0;
                        screen[co + 1] = 0;
                        screen[co + 2] = 0;
                        screen[co + 3] = 0;
                    }
                }
            } else if (gce.disposal === 3 && before) {
                screen.set(before);
            }

            // 处置标志只对当前帧生效，下一帧默认回到 0
            gce = { disposal: 0, delayMs: gce.delayMs, transparent: -1 };
        }

        if (frames.length === 0) {
            throw new GifError('GIF 中没有可解码的帧');
        }

        return frames;
    }

    global.decodeGifFrames = decodeGif;
})(window);
