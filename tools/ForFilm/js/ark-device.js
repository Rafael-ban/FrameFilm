// Ark 专用 BLE 操作。页面和同源编辑器共用父窗口的这一实例。
(function (root) {
    'use strict';

    var transport = null;
    var active = null;
    var pending = null;
    var writeTail = Promise.resolve();
    function delay(ms) { return new Promise(function (resolve) { setTimeout(resolve, ms); }); }

    function connected() { return !!transport && transport.connected(); }
    function busy() { return !!active; }
    function emit() {
        if (root.dispatchEvent && root.CustomEvent) {
            root.dispatchEvent(new root.CustomEvent('ark-device-state', {
                detail: { connected: connected(), busy: busy(), label: active ? active.label : '' }
            }));
        }
    }
    function failPending(error) {
        if (!pending) return;
        var item = pending;
        pending = null;
        clearTimeout(item.timer);
        item.reject(error);
    }
    function bind(next) {
        failPending(new Error('BLE 连接已变更'));
        transport = next;
        emit();
    }
    function disconnected() {
        failPending(new Error('设备已断开'));
        if (active) active.controller.abort();
        emit();
    }
    function asBytes(value) {
        if (value == null) return new Uint8Array();
        // 同源 iframe 的 Uint8Array 来自另一 realm，不能用 instanceof 判定。
        if (!ArrayBuffer.isView(value) || value.BYTES_PER_ELEMENT !== 1)
            throw new Error('BLE 数据必须为 Uint8Array');
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    }
    function frame(channel, payload) {
        payload = asBytes(payload);
        if (!Number.isInteger(channel) || channel < 0 || channel > 255 || payload.length > 192)
            throw new Error('BLE 命令参数无效');
        var bytes = new Uint8Array(payload.length + 4);
        bytes[0] = 0x55; bytes[1] = channel; bytes[2] = payload.length;
        bytes.set(payload, 3);
        var sum = 0;
        for (var i = 0; i < bytes.length - 1; i++) sum += bytes[i];
        bytes[bytes.length - 1] = sum & 255;
        return bytes;
    }
    function valid(bytes) {
        if (!(bytes instanceof Uint8Array) || bytes.length < 4 || bytes[0] !== 0x55 || bytes[2] + 4 !== bytes.length) return false;
        var sum = 0;
        for (var i = 0; i < bytes.length - 1; i++) sum += bytes[i];
        return (sum & 255) === bytes[bytes.length - 1];
    }
    function notification(bytes) {
        if (!pending || !valid(bytes) || bytes[1] !== pending.channel) return false;
        var item = pending;
        pending = null;
        clearTimeout(item.timer);
        item.resolve(bytes.slice(3, -1));
        return true;
    }
    function check(task) {
        if (task.controller.signal.aborted) throw new Error('操作已取消');
        if (!connected()) throw new Error('Ark 未连接');
    }
    function serialWrite(task, channel, payload) {
        var bytes = frame(channel, payload || new Uint8Array());
        var work = writeTail.catch(function () {}).then(function () {
            check(task);
            return transport.write(bytes);
        });
        writeTail = work.catch(function () {});
        return work;
    }
    async function send(task, channel, payload) {
        if (channel === 0x10 || channel === 0x12) task.otaOpen = true;
        await serialWrite(task, channel, payload);
        check(task);
        if (channel === 0x13) task.otaOpen = false;
    }
    async function request(task, channel, payload, timeoutMs) {
        check(task);
        if (pending) throw new Error('上一条 Ark 查询尚未结束');
        var wait = new Promise(function (resolve, reject) {
            pending = { channel: channel, resolve: resolve, reject: reject, timer: null };
        });
        // 断连可以在 writeValue 仍未返回时拒绝此 Promise；立即附加 rejection handler。
        wait.catch(function () {});
        var item = pending;
        try {
            await serialWrite(task, channel, payload);
            check(task);
            if (pending === item) {
                item.timer = setTimeout(function () { failPending(new Error('设备未响应')); }, timeoutMs || 7000);
            }
            return await wait;
        } catch (error) {
            if (pending === item) failPending(error);
            throw error;
        }
    }
    function u32(value) {
        return new Uint8Array([(value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255]);
    }
    async function uploadFile(task, path, bytes, onProgress) {
        bytes = asBytes(bytes);
        if (typeof path !== 'string' || !path || path.length > 63 || !bytes.length ||
            !/^[A-Za-z0-9 _.-]+(?:\/[A-Za-z0-9 _.-]+)*$/.test(path) ||
            path.split('/').some(function (part) { return part === '.' || part === '..' || !part.trim(); }))
            throw new Error('文件路径或内容无效');
        var name = new Uint8Array(path.length + 1);
        for (var i = 0; i < path.length; i++) name[i] = path.charCodeAt(i);
        task.uploadOpen = true;
        await send(task, 0x03);
        await delay(50);
        await send(task, 0x00, name);
        await delay(50);
        await send(task, 0x01, u32(bytes.length));
        await delay(50);
        var chunks = 0;
        for (var offset = 0; offset < bytes.length; offset += 192) {
            await send(task, 0x02, bytes.slice(offset, offset + 192));
            await delay(4);
            chunks++;
            if (chunks % 32 === 0) await fileBarrier(task);
            if (onProgress) onProgress(Math.min(offset + 192, bytes.length), bytes.length);
        }
        await send(task, 0x04);
        await delay(50);
        await fileBarrier(task);
        task.uploadOpen = false;
        return { sent: bytes.length, confirmed: false };
    }
    async function fileBarrier(task) {
        var screen = await request(task, 0x42, new Uint8Array(), 15000);
        if (screen.length !== 5) throw new Error('设备处理屏障响应无效');
    }
    async function run(label, operation) {
        if (active) throw new Error('已有 Ark 操作进行中：' + active.label);
        if (!connected()) throw new Error('请先连接 Ark');
        if (transport.canRun && !transport.canRun()) throw new Error('已有文件传输进行中，请完成或重新连接后再试');
        var task = { label: String(label || 'Ark 操作'), controller: new AbortController(), uploadOpen: false, otaOpen: false };
        active = task;
        emit();
        var ctx = {
            signal: task.controller.signal,
            send: function (ch, data) { return send(task, ch, data); },
            request: function (ch, data, timeout) { return request(task, ch, data, timeout); },
            uploadFile: function (path, data, progress) { return uploadFile(task, path, data, progress); }
        };
        try { return await operation(ctx); }
        finally {
            if (pending) failPending(new Error('操作已结束'));
            if ((task.uploadOpen || task.otaOpen) && transport && transport.disconnect) transport.disconnect();
            active = null;
            emit();
        }
    }
    function cancel() {
        if (!active) return false;
        active.controller.abort();
        failPending(new Error('操作已取消'));
        if (transport && transport.disconnect) transport.disconnect();
        emit();
        return true;
    }
    async function reconnect() {
        if (active) throw new Error('操作进行中，不能重连');
        if (!transport || !transport.reconnect) throw new Error('当前浏览器没有已授权 Ark 设备');
        await transport.reconnect();
        emit();
        return connected();
    }
    root.ArkDevice = { connected: connected, busy: busy, run: run, cancel: cancel,
        reconnect: reconnect, bind: bind, disconnected: disconnected, notification: notification };
})(window);
