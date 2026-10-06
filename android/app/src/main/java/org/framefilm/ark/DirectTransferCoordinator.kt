package org.framefilm.ark

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.io.File

/** One transfer attempt. The BLE connection belongs to ArkSession; this owns only its P2P group. */
class DirectTransferCoordinator(
    context: Context,
    private val ble: ArkBleController,
    private val file: File,
    private val fileName: String,
    private val listener: Listener,
) {
    interface Listener { fun onSnapshot(snapshot: TransferSnapshot) }

    private val main = Handler(Looper.getMainLooper())
    private val getterChannels = intArrayOf(0x31, 0x33, 0x35, 0x37)
    private val before = mutableListOf<ByteArray>()
    private val after = mutableListOf<ByteArray>()
    private val p2p: ArkP2pController = ArkP2pController(context, object : ArkP2pController.Listener {
        override fun onGroupReady(frequencyMhz: Int) {
            if (active && !stopping) emit("connecting", "2.4GHz GO 已就绪（${frequencyMhz}MHz）")
        }
        override fun onSessionReady(session: ArkP2pController.Session) {
            if (!active || stopping) return
            try {
                server = FilmHttpServer(file, fileName, session.ownerAddress).also { it.start() }
                val payload = ByteArrayOutputStream().apply {
                    write(session.ssid.toByteArray(Charsets.US_ASCII)); write(0)
                    write(session.passphrase.toByteArray(Charsets.US_ASCII)); write(0)
                    write(server!!.url.toByteArray(Charsets.US_ASCII)); write(0)
                }.toByteArray()
                if (payload.size > 192) { stop("直传参数超过 BLE 单包上限"); return }
                ble.sendNoReply(0x4B, byteArrayOf(0)) { result ->
                    if (!active || stopping) return@sendNoReply
                    result.getOrElse { stop(it.message ?: "无法切换图片 app"); return@sendNoReply }
                    startSubmitted = true
                    ble.command(0x50, payload, 8_000) { reply ->
                        if (!active || stopping) return@command
                        val code = reply.getOrElse { stop(it.message ?: "直传启动失败"); return@command }
                        if (code.size != 1 || code[0].toInt() != 0) {
                            startSubmitted = false
                            stop("Ark 拒绝直传（代码 ${code.firstOrNull()?.toInt()?.and(0xff) ?: -1}）")
                        } else {
                            emit("connecting", "Ark 已接受直传；文件 ${server!!.size} 字节")
                            poll()
                        }
                    }
                }
            } catch (error: Exception) { stop("手机 HTTP 服务启动失败：${error.message}") }
        }
        override fun onState(state: String) {
            if (active && !stopping) emit("connecting", "Wi-Fi Direct：$state")
        }
        override fun onError(message: String) {
            if (active && !stopping) stop("Wi-Fi Direct：$message")
            else if (cleaning) cleanupError = message
        }
    })

    private var server: FilmHttpServer? = null
    private var active = false
    private var stopping = false
    private var cleaning = false
    private var startSubmitted = false
    private var terminal = false
    private var p2pClean = false
    private var cleanupError: String? = null
    private var resultPhase = "error"
    private var resultMessage = "直传未完成"
    private var resultSuccess = false
    private var received = 0L
    private var total = 0L
    private var deadline: Runnable? = null

    val isActive: Boolean get() = active || cleaning
    val cleanupConfirmed: Boolean get() = p2pClean && (!startSubmitted || terminal)

    fun start() = onMain {
        if (active || cleaning) return@onMain
        if (!ble.isReady) { emit("error", "请先连接 Ark", canRetry = true); return@onMain }
        if (!file.isFile || file.length() !in 1..0xffffffffL) {
            emit("error", "film 文件不存在或大小无效", canRetry = false)
            return@onMain
        }
        active = true
        total = file.length()
        deadline = Runnable { stop("直传超过 180 秒") }.also { main.postDelayed(it, 180_000) }
        emit("preparing", "核对 Ark 屏幕及原有 Wi-Fi 配置")
        ble.command(0x42) { result ->
            if (!active || stopping) return@command
            val data = result.getOrElse { stop(it.message ?: "读取屏幕参数失败"); return@command }
            val width = if (data.size == 5) ((data[1].toInt() and 0xff) shl 8) or (data[2].toInt() and 0xff) else -1
            val height = if (data.size == 5) ((data[3].toInt() and 0xff) shl 8) or (data[4].toInt() and 0xff) else -1
            if (data.size != 5 || data[0].toInt() and 0xff != 2 || width != 720 || height != 480) {
                stop("设备不是预期 Ark 720×480 面板")
            } else snapshot(0, before) {
                if (active && !stopping) {
                    emit("connecting", "创建 2.4GHz 直连组")
                    p2p.createGroup2Ghz()
                }
            }
        }
    }

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun emit(phase: String, message: String, canRetry: Boolean = false, cleanupCompleted: Boolean = false,
                     success: Boolean = false) {
        listener.onSnapshot(TransferSnapshot(phase, message, received, total,
            canCancel = active && !stopping, canRetry = canRetry, cleanupCompleted = cleanupCompleted,
            success = success))
    }

    private fun snapshot(index: Int, target: MutableList<ByteArray>, complete: () -> Unit) {
        if (!active || stopping) return
        if (index == getterChannels.size) { complete(); return }
        ble.command(getterChannels[index]) { result ->
            if (!active || stopping) return@command
            target += result.getOrElse { stop("读取原有 Wi-Fi 配置失败：${it.message}"); return@command }
            snapshot(index + 1, target, complete)
        }
    }

    private fun poll() {
        if (!active || stopping || terminal) return
        ble.command(0x51) { result ->
            if (!active || stopping) return@command
            val bytes = result.getOrElse { stop(it.message ?: "读取直传状态失败"); return@command }
            if (bytes.size != 11) { stop("直传状态长度错误"); return@command }
            val state = bytes[0].toInt() and 0xff
            val error = bytes[2].toInt() and 0xff
            fun u32(offset: Int): Long = (0..3).fold(0L) { value, n -> (value shl 8) or (bytes[offset + n].toLong() and 0xff) }
            received = u32(3)
            val reportedTotal = u32(7)
            if (reportedTotal > 0) total = reportedTotal
            val phase = when (state) { 1 -> "connecting"; 2 -> "downloading"; 3 -> "restoring"; else -> "downloading" }
            emit(phase, "Ark 状态 $state，${bytes[1].toInt() and 0xff}%，$received/$total 字节")
            if (state in 4..6) {
                terminal = true
                if (state != 4) { stop("Ark 直传失败：状态 $state，错误 $error"); return@command }
                val expected = server?.size ?: -1L
                if (received != expected || reportedTotal != expected) {
                    stop("传输字节数不符：$received/$reportedTotal，预期 $expected")
                    return@command
                }
                snapshot(0, after) {
                    if (before.size != after.size || before.indices.any { !before[it].contentEquals(after[it]) })
                        stop("原有 Wi-Fi 配置前后不一致")
                    else finish("done", "Ark 已保存 $received 字节，原有 Wi-Fi 配置未变", true)
                }
            } else main.postDelayed({ poll() }, 800)
        }
    }

    fun cancel() = onMain { if (active) stop("用户取消直传", cancelled = true) }

    fun onBleDisconnected() = onMain {
        if (active && !stopping) stop("Ark 蓝牙断开；需重连后确认固件恢复终态")
    }

    private fun stop(message: String, cancelled: Boolean = false) {
        if (!active || stopping) return
        stopping = true
        resultPhase = if (cancelled) "cancelled" else "error"
        resultMessage = message
        resultSuccess = false
        if (startSubmitted && !terminal && ble.isReady) {
            emit("cancelling", "请求 Ark 取消并等待 Wi-Fi 恢复")
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
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            if (state == 0 || state in 4..6) { terminal = true; cleanup() }
            else main.postDelayed({ waitTerminal(until) }, 800)
        }
    }

    private fun finish(phase: String, message: String, success: Boolean) {
        resultPhase = phase
        resultMessage = message
        resultSuccess = success
        cleanup()
    }

    private fun cleanup() {
        if (!active || cleaning) return
        cleaning = true
        active = false
        deadline?.let(main::removeCallbacks)
        deadline = null
        emit("cleanup", "关闭临时 HTTP 服务并释放手机直连组")
        server?.close()
        server = null
        p2p.close { clean ->
            cleaning = false
            p2pClean = clean
            val complete = cleanupConfirmed
            val success = resultSuccess && complete
            val phase = if (!complete) "error" else resultPhase
            val detail = when {
                !clean -> "; 手机直连组清理未确认：${cleanupError ?: "原因未报告"}"
                startSubmitted && !terminal -> "; Ark 恢复终态未确认"
                else -> ""
            }
            emit(phase, resultMessage + detail, canRetry = !success && file.isFile,
                cleanupCompleted = complete, success = success)
        }
    }

    /** Called before another attempt; never creates a new group until the old session converges. */
    fun ensureSettled(callback: (Boolean, String) -> Unit) = onMain {
        if (isActive) { callback(false, "上一轮仍在进行"); return@onMain }
        fun settleGroup() {
            if (p2pClean && !p2p.hasPendingCleanup) { callback(true, "上一轮已清理"); return }
            p2p.close { clean ->
                p2pClean = clean
                callback(clean, if (clean) "上一轮直连组已释放" else "上一轮直连组仍未清理")
            }
        }
        if (!startSubmitted || terminal) { settleGroup(); return@onMain }
        if (!ble.isReady) {
            // The firmware state is unknown, but the phone-owned group can still be released.
            p2p.close { clean ->
                p2pClean = clean
                callback(false, "请先重连 Ark，再确认上一轮 Wi-Fi 已恢复")
            }
            return@onMain
        }
        settleFirmware(System.currentTimeMillis() + 40_000) { restored ->
            if (restored) settleGroup()
            else callback(false, "上一轮 Ark Wi-Fi 恢复终态未确认")
        }
    }

    private fun settleFirmware(until: Long, callback: (Boolean) -> Unit) {
        if (!ble.isReady || System.currentTimeMillis() >= until) { callback(false); return }
        if (ble.isBusy) { main.postDelayed({ settleFirmware(until, callback) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            when {
                state == 0 || state in 4..6 -> { terminal = true; callback(true) }
                state in 1..3 -> ble.command(0x52) { waitSettledStatus(until, callback) }
                else -> callback(false)
            }
        }
    }

    private fun waitSettledStatus(until: Long, callback: (Boolean) -> Unit) {
        if (!ble.isReady || System.currentTimeMillis() >= until) { callback(false); return }
        if (ble.isBusy) { main.postDelayed({ waitSettledStatus(until, callback) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            if (state == 0 || state in 4..6) { terminal = true; callback(true) }
            else main.postDelayed({ waitSettledStatus(until, callback) }, 800)
        }
    }
}
