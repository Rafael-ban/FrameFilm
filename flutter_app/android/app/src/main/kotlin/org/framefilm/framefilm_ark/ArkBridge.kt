package org.framefilm.framefilm_ark

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import org.framefilm.ark.ArkSession

/** One activity-owned session; all state and callbacks run on the Android main thread. */
class ArkBridge(private val activity: Activity, messenger: BinaryMessenger) :
    MethodChannel.MethodCallHandler, EventChannel.StreamHandler {
    private val main = Handler(Looper.getMainLooper())
    private val methods = MethodChannel(messenger, "org.framefilm.ark/methods")
    private val events = EventChannel(messenger, "org.framefilm.ark/events")
    private var sink: EventChannel.EventSink? = null
    private var closed = false
    private var connected = false
    private var message = "请扫描并连接 Ark"
    private var devices: List<Map<String, String>> = emptyList()
    private var name: String? = null
    private var battery: Int? = null
    private var width: Int? = null
    private var height: Int? = null
    private var permissionAction: (() -> Unit)? = null
    private var permissionResult: MethodChannel.Result? = null
    private var refreshResult: MethodChannel.Result? = null
    private var refreshing = false
    private var readIndex = 0
    private val channels = intArrayOf(0x42, 0x23, 0x54)
    private class Read(val channel: Int) {
        var writeDone = false
        var response: ByteArray? = null
        var timeout: Runnable? = null
    }
    private var pending: Read? = null
    private val session: ArkSession = ArkSession(activity, object : ArkSession.Listener {
        override fun onDevices(found: List<BluetoothDevice>) {
            if (closed) return
            devices = found.map { device ->
                mapOf("address" to device.address,
                    "name" to (session.scannedName(device.address) ?: "Ark"))
            }
            emit()
        }
        override fun onConnection(ready: Boolean, state: String) {
            if (closed) return
            val becameReady = ready && !connected
            connected = ready
            message = state
            if (!ready) {
                clearRead("disconnected", state)
                clearDetails()
            }
            emit()
            if (becameReady) beginRefresh(null)
        }
        override fun onPacket(packet: ByteArray) {
            val read = pending ?: return
            if (closed || !connected || packet.size < 4) return
            val size = packet[2].toInt() and 255
            if ((packet[1].toInt() and 255) != read.channel || packet.size != size + 4) return
            read.response = packet.copyOfRange(3, size + 3)
            completeRead(read)
        }
    })

    init {
        methods.setMethodCallHandler(this)
        events.setStreamHandler(this)
    }

    private fun snapshot(): Map<String, Any?> = mapOf(
        "connected" to connected, "message" to message, "devices" to devices,
        "name" to name, "battery" to battery, "width" to width, "height" to height)

    private fun emit() { if (!closed) sink?.success(snapshot()) }

    override fun onListen(arguments: Any?, eventSink: EventChannel.EventSink) {
        sink = eventSink
        emit()
    }

    override fun onCancel(arguments: Any?) { sink = null }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        if (closed) { result.error("closed", "蓝牙会话已关闭", null); return }
        try {
            when (call.method) {
                "snapshot" -> { emit(); result.success(snapshot()) }
                "scan" -> withPermissions(result) {
                    session.scan()
                    result.success(snapshot())
                }
                "connect" -> {
                    val address = call.argument<String>("address")
                    require(!address.isNullOrBlank()) { "请选择 Ark 设备" }
                    require(devices.any { it["address"] == address }) { "请先扫描并选择 Ark" }
                    withPermissions(result) {
                        clearRead("cancelled", "已切换设备")
                        clearDetails()
                        connected = false
                        session.connect(address)
                        result.success(snapshot())
                    }
                }
                "disconnect" -> {
                    clearRead("cancelled", "已断开连接")
                    session.disconnect()
                    connected = false
                    clearDetails()
                    message = "已断开连接"
                    emit()
                    result.success(snapshot())
                }
                "refresh" -> withPermissions(result) { beginRefresh(result) }
                else -> result.notImplemented()
            }
        } catch (error: Exception) {
            result.error("operation_failed", error.message ?: "操作失败", null)
        }
    }

    private fun withPermissions(result: MethodChannel.Result, action: () -> Unit) {
        val required = when {
            Build.VERSION.SDK_INT >= 31 -> arrayOf(Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT)
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = required.filter { activity.checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) { action(); return }
        if (permissionResult != null) {
            result.error("busy", "请先完成蓝牙权限授权", null)
            return
        }
        permissionResult = result
        permissionAction = action
        try { activity.requestPermissions(missing.toTypedArray(), PERMISSION_REQUEST) }
        catch (error: Exception) {
            permissionResult = null
            permissionAction = null
            throw error
        }
    }

    fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>,
                                   grantResults: IntArray): Boolean {
        if (requestCode != PERMISSION_REQUEST) return false
        val result = permissionResult
        val action = permissionAction
        permissionResult = null
        permissionAction = null
        if (closed || result == null) return true
        if (grantResults.isEmpty() || grantResults.any { it != PackageManager.PERMISSION_GRANTED }) {
            message = "需要附近设备权限才能扫描和连接 Ark，请在系统设置中允许"
            emit()
            result.error("permission_denied", message, null)
        } else {
            try { action?.invoke() }
            catch (error: Exception) { result.error("operation_failed", error.message, null) }
        }
        return true
    }

    private fun beginRefresh(result: MethodChannel.Result?) {
        if (!connected || !session.isReady) {
            result?.error("not_connected", "请先连接 Ark", null)
            return
        }
        if (refreshing) {
            // A user refresh may join the automatic first read.
            if (result != null && refreshResult == null) refreshResult = result
            else result?.error("busy", "设备信息正在读取", null)
            return
        }
        refreshing = true
        refreshResult = result
        readIndex = 0
        readNext()
    }

    private fun readNext() {
        if (readIndex == channels.size) {
            refreshing = false
            message = "设备信息已更新"
            emit()
            val result = refreshResult
            refreshResult = null
            result?.success(snapshot())
            return
        }
        val read = Read(channels[readIndex])
        pending = read
        message = "正在读取设备信息"
        emit()
        read.timeout = Runnable {
            if (pending === read) failRead("timeout", "设备响应超时，请重新连接 Ark")
        }.also { main.postDelayed(it, 7_000) }
        val bytes = byteArrayOf(0x55, read.channel.toByte(), 0)
        session.writePacket(bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())) { outcome ->
            if (closed || pending !== read) return@writePacket
            outcome.fold(onSuccess = {
                read.writeDone = true
                completeRead(read)
            }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
        }
    }

    private fun completeRead(read: Read) {
        if (pending !== read || !read.writeDone) return
        val data = read.response ?: return
        val valid = when (read.channel) {
            0x42 -> data.size == 5 && u16(data, 1) > 0 && u16(data, 3) > 0
            0x23 -> data.size == 1 && (data[0].toInt() and 255) in 0..100
            0x54 -> data.size in 3..31 && data[0] == 0.toByte() && data.last() == 0.toByte()
            else -> false
        }
        if (!valid) { failRead("invalid_response", "设备返回的信息格式无效"); return }
        when (read.channel) {
            0x42 -> { width = u16(data, 1); height = u16(data, 3) }
            0x23 -> battery = data[0].toInt() and 255
            0x54 -> name = data.copyOfRange(1, data.lastIndex).toString(Charsets.UTF_8)
        }
        read.timeout?.let(main::removeCallbacks)
        pending = null
        emit()
        readIndex++
        readNext()
    }

    private fun failRead(code: String, detail: String) {
        clearRead(code, detail)
        // Reconnect creates a new GATT generation, so a late reply cannot satisfy a retry.
        session.disconnect()
        connected = false
        clearDetails()
        message = detail
        emit()
    }

    private fun clearRead(code: String, detail: String) {
        pending?.timeout?.let(main::removeCallbacks)
        pending = null
        refreshing = false
        val result = refreshResult
        refreshResult = null
        result?.error(code, detail, null)
    }

    private fun clearDetails() { name = null; battery = null; width = null; height = null }
    private fun u16(data: ByteArray, offset: Int) =
        ((data[offset].toInt() and 255) shl 8) or (data[offset + 1].toInt() and 255)

    fun close() {
        if (closed) return
        closed = true
        clearRead("closed", "蓝牙会话已关闭")
        permissionResult?.error("closed", "蓝牙会话已关闭", null)
        permissionResult = null
        permissionAction = null
        methods.setMethodCallHandler(null)
        events.setStreamHandler(null)
        sink = null
        session.close()
    }

    private companion object { const val PERMISSION_REQUEST = 4207 }
}
