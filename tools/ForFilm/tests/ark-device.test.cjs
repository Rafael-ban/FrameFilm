const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../js/ark-device.js'), 'utf8');

function create(options = {}) {
    const events = [];
    const writes = [];
    let connected = true;
    let disconnected = false;
    const window = {
        CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options.detail; } },
        dispatchEvent(event) { events.push(event); }
    };
    const context = vm.createContext({ window, Uint8Array, TextEncoder, AbortController, Promise, Error,
        Number, setTimeout, clearTimeout });
    vm.runInContext(source, context);
    window.ArkDevice.bind({
        connected: () => connected,
        write: async bytes => {
            writes.push(bytes);
            if (options.hold && bytes[1] === options.hold.channel) await options.hold.promise;
            if (bytes[1] === 0x42) queueMicrotask(() => window.ArkDevice.notification(response(0x42, new Uint8Array(5))));
        },
        disconnect: () => { disconnected = true; connected = false; window.ArkDevice.disconnected(); },
        reconnect: async () => { connected = true; }
    });
    return { api: window.ArkDevice, writes, events, get disconnected() { return disconnected; } };
}

function response(channel, payload, corrupt = false) {
    const bytes = new Uint8Array(payload.length + 4);
    bytes[0] = 0x55; bytes[1] = channel; bytes[2] = payload.length;
    bytes.set(payload, 3);
    bytes[bytes.length - 1] = bytes.slice(0, -1).reduce((sum, b) => sum + b, 0) & 255;
    if (corrupt) bytes[bytes.length - 1] ^= 1;
    return bytes;
}

test('request ignores other and malformed frames, then returns matching payload', async () => {
    const { api, writes } = create();
    const work = api.run('query', ctx => ctx.request(0x51, new Uint8Array(), 1000));
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(writes.length, 1);
    assert.equal(api.notification(response(0x52, new Uint8Array([1]))), false);
    assert.equal(api.notification(response(0x51, new Uint8Array([1]), true)), false);
    assert.equal(api.notification(response(0x51, new Uint8Array([4]))), true);
    assert.deepEqual(Array.from(await work), [4]);
    assert.equal(api.busy(), false);
});

test('run is exclusive and upload starts from FILE_START with chunked data', async () => {
    const { api, writes } = create();
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    const first = api.run('upload', async ctx => {
        await gate;
        return ctx.uploadFile('app/pass/profile.json', new Uint8Array(200));
    });
    await assert.rejects(api.run('other', async () => {}), /已有 Ark 操作/);
    release();
    const result = await first;
    assert.equal(result.confirmed, false);
    assert.deepEqual(writes.map(frame => frame[1]), [0x03, 0x00, 0x01, 0x02, 0x02, 0x04, 0x42]);
    assert.deepEqual(writes.filter(frame => frame[1] === 0x02).map(frame => frame[2]), [192, 8]);
    assert.equal(writes.find(frame => frame[1] === 0x00).at(-2), 0);
});

test('upload inserts a processing barrier after 32 data chunks', async () => {
    const { api, writes } = create();
    await api.run('paced upload', ctx => ctx.uploadFile('photo.film', new Uint8Array(33 * 192)));
    const channels = writes.map(frame => frame[1]);
    assert.equal(channels.filter(ch => ch === 0x42).length, 2);
    assert.equal(channels[35], 0x42);
    assert.deepEqual(channels.slice(-3), [0x02, 0x04, 0x42]);
});

test('incomplete upload disconnects and rejects unsafe path', async () => {
    const { api, writes } = create();
    await assert.rejects(api.run('bad path', ctx => ctx.uploadFile('../profile.json', new Uint8Array([1]))), /路径/);
    assert.equal(writes.length, 0);
    const task = api.run('partial upload', ctx => ctx.uploadFile('photo.film', new Uint8Array(400), () => { throw new Error('editor failed'); }));
    await assert.rejects(task, /editor failed/);
    assert.equal(api.connected(), false);
    assert.equal(writes.some(frame => frame[1] === 0x04), false);
});

test('accepts Uint8Array produced by a same-origin editor realm', async () => {
    const { api, writes } = create();
    const foreignBytes = vm.runInNewContext('new Uint8Array([1, 2, 3])');
    await api.run('editor', ctx => ctx.send(0x4B, foreignBytes));
    assert.deepEqual(Array.from(writes[0].slice(3, 6)), [1, 2, 3]);
});

test('cancel disconnects and stops an upload before another chunk', async () => {
    const { api, writes, events } = create();
    const work = api.run('upload', async ctx => {
        await ctx.uploadFile('photo.film', new Uint8Array(400), () => api.cancel());
    });
    await assert.rejects(work, /操作已取消|设备已断开/);
    assert.equal(writes.filter(frame => frame[1] === 0x02).length, 1);
    assert.equal(writes.some(frame => frame[1] === 0x04), false);
    assert.equal(api.busy(), false);
    assert.equal(events.at(-1).detail.connected, false);
});

test('disconnect rejects pending request and reconnect uses authorized transport', async () => {
    const { api } = create();
    const work = api.run('query', ctx => ctx.request(0x51, new Uint8Array(), 1000));
    await new Promise(resolve => setImmediate(resolve));
    api.cancel();
    await assert.rejects(work, /操作已取消|设备已断开/);
    assert.equal(await api.reconnect(), true);
});

test('disconnect while request write is pending rejects without unhandled promise', async () => {
    let release;
    const hold = { channel: 0x51, promise: new Promise(resolve => { release = resolve; }) };
    const { api, writes } = create({ hold });
    const work = api.run('slow query', ctx => ctx.request(0x51, new Uint8Array(), 1000));
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(writes.length, 1);
    api.cancel();
    release();
    await assert.rejects(work, /操作已取消|设备已断开/);
});
