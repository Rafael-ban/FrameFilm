package org.framefilm.ark

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.util.ArrayDeque

/** Owns one Ark BLE connection and serializes ordinary writes around direct transfers. */
class ArkSession(context: Context, private val listener: Listener) {
    interface Listener {
        fun onDevices(devices: List<BluetoothDevice>) { }
        fun onConnection(connected: Boolean, message: String) { }
        fun onPacket(packet: ByteArray) { }
        fun onTransfer(snapshot: TransferSnapshot) { }
    }

    private data class QueuedWrite(val channel: Int, val payload: ByteArray,
                                   val callback: (Result<Unit>) -> Unit)
    private data class TransferInput(val file: File, val fileName: String)

    private val main = Handler(Looper.getMainLooper())
    private val appContext = context.applicationContext
    private val devices = linkedMapOf<String, BluetoothDevice>()
    private val writes = ArrayDeque<QueuedWrite>()
    private var writing = false
    private var pendingStart: TransferInput? = null
    private var cancelAfterSettle = false
    private var lastInput: TransferInput? = null
    private var settling = false
    private var disconnectAfterTransfer = false
    private var closing = false
    private var closed = false
    private var coordinator: DirectTransferCoordinator? = null
    private val ble: ArkBleController = ArkBleController(context, object : ArkBleController.Listener {
        override fun onDevices(found: List<BluetoothDevice>) {
            devices.clear()
            found.forEach { devices[it.address] = it }
            if (!closed) listener.onDevices(found)
        }
        override fun onReady() {
            if (!closed) listener.onConnection(true, "Ark GATT 已就绪，MTU ${ble.negotiatedMtu}")
            pump()
        }
        override fun onPanel(panelId: Int, width: Int, height: Int) { }
        override fun onState(state: String) {
            if (!closed) listener.onConnection(ble.isReady, state)
        }
        override fun onError(message: String) {
            if (!closed) listener.onConnection(false, message)
            if (!ble.isReady) {
                coordinator?.onBleDisconnected()
                failQueuedWrites("Ark 蓝牙已断开")
            }
        }
        override fun onPacket(packet: ByteArray) {
            // Transfer-side Wi-Fi snapshot getters can contain saved credentials.
            if (!closed && !transferActive) listener.onPacket(packet)
        }
    })

    val isReady: Boolean get() = ble.isReady
    fun scannedName(address: String): String? = ble.scannedName(address)
    val transferActive: Boolean get() = pendingStart != null || settling || coordinator?.isActive == true

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    fun scan() = onMain {
        if (closed || closing || transferActive) {
            listener.onConnection(isReady, "直传或关闭期间不能扫描")
        } else ble.startScan()
    }

    fun connect(address: String) = onMain {
        if (closed || closing || transferActive) {
            listener.onConnection(isReady, "直传或关闭期间不能切换设备")
            return@onMain
        }
        val device = devices[address]
        if (device == null) listener.onConnection(false, "请先扫描并选择 Ark")
        else ble.connect(device)
    }

    fun disconnect() = onMain {
        if (transferActive) {
            disconnectAfterTransfer = true
            cancelTransfer()
        } else {
            failQueuedWrites("Ark 蓝牙已断开")
            val previous = coordinator
            if (previous != null && !previous.cleanupConfirmed) {
                settling = true
                previous.ensureSettled { complete, message ->
                    settling = false
                    cancelAfterSettle = false
                    disconnectAfterTransfer = false
                    if (!complete) listener.onTransfer(TransferSnapshot("error", message,
                        canRetry = lastInput?.file?.isFile == true, cleanupCompleted = false))
                    if (closing) finishClose() else ble.close()
                }
            } else ble.close()
        }
    }

    /** Callback confirms GATT write completion; notifications arrive separately via onPacket. */
    fun writePacket(packet: ByteArray, callback: (Result<Unit>) -> Unit) = onMain {
        val error = when {
            closed || closing -> "会话已关闭"
            transferActive -> "直传期间不能发送普通 BLE 命令"
            !ble.isReady -> "请先连接 Ark"
            packet.size !in 4..196 || packet[0].toInt() and 0xff != 0x55 -> "BLE 帧无效"
            (packet[2].toInt() and 0xff) + 4 != packet.size -> "BLE 帧长度不匹配"
            (packet.dropLast(1).sumOf { it.toInt() and 0xff } and 0xff) != (packet.last().toInt() and 0xff) -> "BLE 校验和错误"
            else -> null
        }
        if (error != null) { callback(Result.failure(IllegalArgumentException(error))); return@onMain }
        writes.addLast(QueuedWrite(packet[1].toInt() and 0xff, packet.copyOfRange(3, packet.lastIndex), callback))
        pump()
    }

    private fun pump() {
        if (closed || closing || writing || !ble.isReady) return
        if (writes.isNotEmpty()) {
            val next = writes.removeFirst()
            writing = true
            ble.sendNoReply(next.channel, next.payload) { result ->
                writing = false
                next.callback(result)
                pump()
            }
            return
        }
        val input = pendingStart ?: return
        if (ble.isBusy) { main.postDelayed({ pump() }, 100); return }
        pendingStart = null
        settling = true
        val previous = coordinator
        val begin: (Boolean, String) -> Unit = { ready, message ->
            settling = false
            if (cancelAfterSettle) {
                cancelAfterSettle = false
                listener.onTransfer(TransferSnapshot(if (ready) "cancelled" else "error",
                    if (ready) "重试前已取消" else message, total = input.file.length(),
                    canRetry = input.file.isFile, cleanupCompleted = previous?.cleanupConfirmed ?: true))
                if (closing) finishClose()
                else if (disconnectAfterTransfer) { disconnectAfterTransfer = false; ble.close() }
            } else if (!ready || !ble.isReady || closing) {
                listener.onTransfer(TransferSnapshot("error", message,
                    canRetry = input.file.isFile, cleanupCompleted = previous?.cleanupConfirmed ?: true))
                if (closing) finishClose()
            } else {
                val next = DirectTransferCoordinator(appContext, ble, input.file, input.fileName,
                    object : DirectTransferCoordinator.Listener {
                        override fun onSnapshot(snapshot: TransferSnapshot) {
                            if (!closed) listener.onTransfer(snapshot)
                            if (snapshot.phase in setOf("done", "cancelled", "error") &&
                                coordinator?.isActive == false) {
                                if (closing) finishClose()
                                else if (disconnectAfterTransfer) {
                                    disconnectAfterTransfer = false
                                    ble.close()
                                }
                            }
                        }
                    })
                coordinator = next
                next.start()
            }
        }
        if (previous == null) begin(true, "") else previous.ensureSettled(begin)
    }

    fun startTransfer(file: File, fileName: String) = onMain {
        if (closed || closing) {
            listener.onTransfer(TransferSnapshot("error", "会话已关闭",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            return@onMain
        }
        if (transferActive) {
            listener.onConnection(ble.isReady, "已有直传正在进行")
            return@onMain
        }
        if (!file.isFile || file.length() !in 1..0xffffffffL) {
            listener.onTransfer(TransferSnapshot("error", "film 文件不存在或大小无效",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            return@onMain
        }
        val input = TransferInput(file, fileName)
        lastInput = input
        if (!ble.isReady) {
            listener.onTransfer(TransferSnapshot("error", "请先连接 Ark", total = file.length(),
                canRetry = true, cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            return@onMain
        }
        pendingStart = input
        listener.onTransfer(TransferSnapshot("preparing", "等待普通 BLE 写入完成", total = file.length(), canCancel = true))
        pump()
    }

    fun cancelTransfer() = onMain {
        val pending = pendingStart
        if (pending != null) {
            pendingStart = null
            listener.onTransfer(TransferSnapshot("cancelled", "发送直传命令前已取消", total = pending.file.length(),
                canRetry = pending.file.isFile, cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            if (closing) finishClose()
            else if (disconnectAfterTransfer) { disconnectAfterTransfer = false; ble.close() }
        } else if (settling) cancelAfterSettle = true
        else coordinator?.cancel()
    }

    fun retryTransfer() = onMain {
        val input = lastInput
        if (input == null || !input.file.isFile) {
            listener.onTransfer(TransferSnapshot("error", "没有可重试的本地 film 文件",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
        } else startTransfer(input.file, input.fileName)
    }

    private fun failQueuedWrites(message: String) {
        while (writes.isNotEmpty()) writes.removeFirst().callback(Result.failure(IllegalStateException(message)))
    }

    fun close() = onMain {
        if (closed || closing) return@onMain
        closing = true
        failQueuedWrites("会话已关闭")
        if (settling) { cancelAfterSettle = true; return@onMain }
        if (pendingStart != null) pendingStart = null
        val attempt = coordinator
        if (attempt?.isActive == true) attempt.cancel()
        else if (attempt != null && !attempt.cleanupConfirmed) {
            attempt.ensureSettled { _, _ -> finishClose() }
        } else finishClose()
    }

    private fun finishClose() {
        if (closed) return
        closed = true
        ble.close()
    }
}
