package org.framefilm.ark

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream

/** One diagnostic transfer. Callbacks and state changes are serialized on main. */
class DirectTransferCoordinator(context: Context, private val listener: Listener) {
    interface Listener {
        fun onProgress(message: String)
        fun onFinished(success: Boolean, message: String, cleanupCompleted: Boolean)
    }

    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val ble: ArkBleController = ArkBleController(context, object : ArkBleController.Listener {
        override fun onDevices(devices: List<BluetoothDevice>) {
            if (!active || connecting || devices.isEmpty()) return
            if (devices.size != 1) { fail("发现多台 Ark，请先关闭其他设备后重试"); return }
            connecting = true
            ble.connect(devices.single())
        }
        override fun onReady() { if (active) ble.readPanel() }
        override fun onPanel(panelId: Int, width: Int, height: Int) {
            if (!active) return
            if (panelId != 2 || width != 720 || height != 480) fail("设备不是预期 Ark 720×480 面板")
            else snapshot(0, before) { p2p.createGroup2Ghz() }
        }
        override fun onState(state: String) { if (active) listener.onProgress("蓝牙：$state") }
        override fun onError(message: String) { if (active) fail("蓝牙：$message") }
    })
    private val p2p: ArkP2pController = ArkP2pController(context, object : ArkP2pController.Listener {
        override fun onGroupReady(frequencyMhz: Int) {
            if (active) listener.onProgress("2.4GHz GO 已就绪（${frequencyMhz}MHz）")
        }
        override fun onSessionReady(session: ArkP2pController.Session) {
            if (!active) return
            try {
                server = FilmHttpServer(appContext, session.ownerAddress).also { it.start() }
                val url = server!!.url
                val payload = ByteArrayOutputStream().apply {
                    write(session.ssid.toByteArray(Charsets.US_ASCII)); write(0)
                    write(session.passphrase.toByteArray(Charsets.US_ASCII)); write(0)
                    write(url.toByteArray(Charsets.US_ASCII)); write(0)
                }.toByteArray()
                if (payload.size > 192) { fail("直传参数超过 BLE 单包上限"); return }
                // Enter the image app so the newly downloaded film can be shown.
                ble.sendNoReply(0x4B, byteArrayOf(0)) { result ->
                    if (!active || stopping) return@sendNoReply
                    result.getOrElse { fail(it.message ?: "无法切换图片 app"); return@sendNoReply }
                    startSubmitted = true
                    ble.command(0x50, payload, 8_000) { reply ->
                        if (!active) return@command
                        // The request may have reached Ark even if its ACK was lost. Cancellation owns it now.
                        if (stopping) return@command
                        val code = reply.getOrElse { fail(it.message ?: "直传启动失败"); return@command }
                        if (code.size != 1 || code[0].toInt() != 0) {
                            startSubmitted = false
                            fail("Ark 拒绝直传（代码 ${code.firstOrNull()?.toInt()?.and(0xff) ?: -1}）")
                        } else {
                            listener.onProgress("Ark 已接受直传；文件 ${server!!.size} 字节")
                            poll()
                        }
                    }
                }
            } catch (error: Exception) { fail("手机 HTTP 服务启动失败：${error.message}") }
        }
        override fun onState(state: String) { if (active) listener.onProgress("Wi-Fi Direct：$state") }
        override fun onError(message: String) {
            if (active) fail("Wi-Fi Direct：$message")
            else if (cleaning) cleanupError = message
        }
    })
    private val getterChannels = intArrayOf(0x31, 0x33, 0x35, 0x37)
    private val before = mutableListOf<ByteArray>()
    private val after = mutableListOf<ByteArray>()
    private var server: FilmHttpServer? = null
    private var active = false
    private var connecting = false
    private var startSubmitted = false
    private var stopping = false
    private var terminal = false
    private var cleaning = false
    private var cleanupError: String? = null
    private var resultMessage = ""
    private var resultSuccess = false
    private var deadline: Runnable? = null

    fun start() {
        if (Looper.myLooper() != Looper.getMainLooper()) { main.post { start() }; return }
        if (active) return
        active = true
        deadline = Runnable { fail("直传超过 180 秒") }.also { main.postDelayed(it, 180_000) }
        listener.onProgress("扫描并连接 Ark")
        ble.startScan()
    }

    private fun snapshot(index: Int, target: MutableList<ByteArray>, complete: () -> Unit) {
        if (!active) return
        if (index == getterChannels.size) { complete(); return }
        ble.command(getterChannels[index]) { result ->
            if (!active) return@command
            val data = result.getOrElse { fail("读取原有 Wi-Fi 配置失败：${it.message}"); return@command }
            target += data
            snapshot(index + 1, target, complete)
        }
    }

    private fun poll() {
        if (!active || terminal || stopping) return
        ble.command(0x51) { result ->
            if (!active || stopping) return@command
            val bytes = result.getOrElse { fail(it.message ?: "读取直传状态失败"); return@command }
            if (bytes.size != 11) { fail("直传状态长度错误"); return@command }
            val state = bytes[0].toInt() and 0xff
            val progress = bytes[1].toInt() and 0xff
            val error = bytes[2].toInt() and 0xff
            fun u32(offset: Int): Long = (0..3).fold(0L) { value, n -> (value shl 8) or (bytes[offset + n].toLong() and 0xff) }
            val received = u32(3)
            val total = u32(7)
            listener.onProgress("直传状态 $state，$progress%，$received/$total 字节")
            if (state in 4..6) {
                terminal = true
                if (state != 4) { fail("Ark 直传失败：状态 $state，错误 $error"); return@command }
                val expected = server?.size?.toLong() ?: -1L
                if (received != expected || total != expected) {
                    fail("传输字节数不符：$received/$total，预期 $expected")
                    return@command
                }
                snapshot(0, after) {
                    if (before.size != after.size || before.indices.any { !before[it].contentEquals(after[it]) })
                        fail("原有 Wi-Fi 配置前后不一致")
                    else finish(true, "PASS：Ark 已保存 $received 字节，原有 Wi-Fi 配置未变；屏幕显示待目视确认")
                }
            } else main.postDelayed({ poll() }, 800)
        }
    }

    fun cancel() {
        if (Looper.myLooper() != Looper.getMainLooper()) { main.post { cancel() }; return }
        if (active) fail("用户取消直传")
    }

    private fun fail(message: String) {
        if (!active || stopping) return
        resultMessage = message
        resultSuccess = false
        stopping = true
        if (startSubmitted && !terminal && ble.isReady) {
            listener.onProgress("请求 Ark 取消并等待 Wi-Fi 恢复")
            requestCancel(System.currentTimeMillis() + 40_000)
        } else cleanup()
    }

    private fun requestCancel(until: Long) {
        if (!active) return
        if (System.currentTimeMillis() >= until || !ble.isReady) { cleanup(); return }
        if (ble.isBusy) { main.postDelayed({ requestCancel(until) }, 200); return }
        ble.command(0x52) { waitTerminal(until) }
    }

    private fun waitTerminal(until: Long) {
        if (!active) return
        if (System.currentTimeMillis() >= until || !ble.isReady) { cleanup(); return }
        if (ble.isBusy) { main.postDelayed({ waitTerminal(until) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            if (bytes?.size == 11 && (bytes[0].toInt() and 0xff) in 4..6) {
                terminal = true
                cleanup()
            } else main.postDelayed({ waitTerminal(until) }, 800)
        }
    }

    private fun finish(success: Boolean, message: String) {
        resultSuccess = success
        resultMessage = message
        cleanup()
    }

    private fun cleanup() {
        if (!active) return
        cleaning = true
        active = false
        deadline?.let(main::removeCallbacks)
        deadline = null
        server?.close()
        server = null
        ble.close()
        p2p.close { clean ->
            cleaning = false
            val complete = clean && (!startSubmitted || terminal)
            val detail = if (!clean) "; 手机直连组清理未确认：${cleanupError ?: "原因未报告"}" else ""
            listener.onFinished(resultSuccess, resultMessage + detail, complete)
        }
    }
}
