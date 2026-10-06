package org.framefilm.ark

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.UUID

/** Small, read-only Ark BLE probe. All public calls and listener callbacks run on main. */
@Suppress("DEPRECATION") // API 29-32 GATT write/callback compatibility.
class ArkBleController(context: Context, private val listener: Listener) {
    interface Listener {
        fun onDevices(devices: List<BluetoothDevice>)
        fun onReady()
        fun onPanel(panelId: Int, width: Int, height: Int)
        fun onState(state: String)
        fun onError(message: String)
    }

    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val adapter = appContext.getSystemService(BluetoothManager::class.java)?.adapter
    private val serviceUuid = UUID.fromString("00002000-0000-1000-8000-00805f9b34fb")
    private val characteristicUuid = UUID.fromString("00002001-0000-1000-8000-00805f9b34fb")
    private val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    private val devices = linkedMapOf<String, BluetoothDevice>()
    private var scanCallback: ScanCallback? = null
    private var gatt: BluetoothGatt? = null
    private var characteristic: BluetoothGattCharacteristic? = null
    private var generation = 0
    private var waitingForPanel = false
    private var timeout: Runnable? = null

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun hasPermissions(): Boolean =
        if (Build.VERSION.SDK_INT >= 31) {
            appContext.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                appContext.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            appContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }

    private fun armTimeout(ms: Long, message: String, action: () -> Unit = {}) {
        timeout?.let(main::removeCallbacks)
        val token = generation
        timeout = Runnable {
            if (token == generation) {
                timeout = null
                action()
                listener.onError(message)
            }
        }.also { main.postDelayed(it, ms) }
    }

    private fun clearTimeout() {
        timeout?.let(main::removeCallbacks)
        timeout = null
    }

    @SuppressLint("MissingPermission")
    fun startScan() = onMain {
        if (!hasPermissions()) { listener.onError("需要附近设备蓝牙权限"); return@onMain }
        val scanner = adapter?.bluetoothLeScanner
        if (adapter?.isEnabled != true || scanner == null) {
            listener.onError("请先开启蓝牙"); return@onMain
        }
        stopScan(false)
        devices.clear()
        listener.onDevices(emptyList())
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                onMain {
                    if (scanCallback !== this) return@onMain
                    val name = result.scanRecord?.deviceName ?: result.device.name
                    if (name == "FRAMEFILMARK") {
                        devices[result.device.address] = result.device
                        listener.onDevices(devices.values.toList())
                    }
                }
            }

            override fun onScanFailed(errorCode: Int) = onMain {
                if (scanCallback === this) {
                    stopScan(false)
                    listener.onError("蓝牙扫描失败：$errorCode")
                }
            }
        }
        scanCallback = callback
        try {
            scanner.startScan(callback)
            listener.onState("扫描 Ark 中")
            main.postDelayed({ if (scanCallback === callback) stopScan() }, 10_000)
        } catch (_: SecurityException) {
            scanCallback = null
            listener.onError("蓝牙扫描权限不可用")
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan(notify: Boolean = true) {
        val callback = scanCallback ?: return
        scanCallback = null
        try { adapter?.bluetoothLeScanner?.stopScan(callback) } catch (_: SecurityException) { }
        if (notify) listener.onState("扫描结束：发现 ${devices.size} 台 Ark")
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) = onMain {
        if (!hasPermissions()) { listener.onError("需要附近设备蓝牙权限"); return@onMain }
        stopScan(false)
        closeGatt()
        val token = generation
        listener.onState("连接 Ark 中")
        try {
            val callback = object : BluetoothGattCallback() {
                override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) = onMain {
                    if (token != generation || g !== gatt) return@onMain
                    if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                        listener.onState("发现 GATT 服务中")
                        if (!g.discoverServices()) fail("无法发现 GATT 服务")
                        else armTimeout(12_000, "GATT 服务发现超时") { closeGatt() }
                    } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        fail("Ark 蓝牙已断开 ($status)")
                    } else if (status != BluetoothGatt.GATT_SUCCESS) {
                        fail("Ark 蓝牙连接失败 ($status)")
                    }
                }

                override fun onServicesDiscovered(g: BluetoothGatt, status: Int) = onMain {
                    if (token != generation || g !== gatt) return@onMain
                    clearTimeout()
                    val ch = g.getService(serviceUuid)?.getCharacteristic(characteristicUuid)
                    val descriptor = ch?.getDescriptor(cccdUuid)
                    if (status != BluetoothGatt.GATT_SUCCESS || ch == null || descriptor == null) {
                        fail("Ark GATT 2000/2001 或通知描述符不可用")
                        return@onMain
                    }
                    characteristic = ch
                    if (!g.setCharacteristicNotification(ch, true)) {
                        fail("无法启用 Ark 通知")
                        return@onMain
                    }
                    val started = if (Build.VERSION.SDK_INT >= 33) {
                        g.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS
                    } else {
                        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        g.writeDescriptor(descriptor)
                    }
                    if (!started) fail("无法写入 Ark 通知配置")
                    else armTimeout(8_000, "启用 Ark 通知超时") { closeGatt() }
                }

                override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) = onMain {
                    if (token != generation || g !== gatt || descriptor.uuid != cccdUuid) return@onMain
                    clearTimeout()
                    if (status != BluetoothGatt.GATT_SUCCESS) fail("Ark 通知配置失败 ($status)")
                    else {
                        listener.onState("Ark GATT 已就绪")
                        listener.onReady()
                    }
                }

                override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray) {
                    onMain { if (token == generation && g === gatt && ch.uuid == characteristicUuid) acceptPanel(value) }
                }

                @Deprecated("Pre-Android 13 callback")
                override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
                    if (Build.VERSION.SDK_INT < 33) {
                        val value = ch.value?.clone() ?: return
                        onMain { if (token == generation && g === gatt && ch.uuid == characteristicUuid) acceptPanel(value) }
                    }
                }
            }
            gatt = device.connectGatt(appContext, false, callback, BluetoothDevice.TRANSPORT_LE)
            if (gatt == null) fail("无法启动 Ark 蓝牙连接")
            else armTimeout(12_000, "连接 Ark 超时") { closeGatt() }
        } catch (_: SecurityException) { fail("蓝牙连接权限不可用") }
    }

    @SuppressLint("MissingPermission")
    fun readPanel() = onMain {
        if (!hasPermissions()) { listener.onError("需要附近设备蓝牙权限"); return@onMain }
        val g = gatt
        val ch = characteristic
        if (g == null || ch == null) { listener.onError("请先连接 Ark 并等待 GATT 就绪"); return@onMain }
        if (waitingForPanel) { listener.onError("屏幕参数查询仍在进行"); return@onMain }
        val packet = byteArrayOf(0x55, 0x42, 0x00, 0x97.toByte())
        waitingForPanel = true
        val started = try {
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(ch, packet, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
            } else {
                ch.value = packet
                ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                g.writeCharacteristic(ch)
            }
        } catch (_: SecurityException) { false }
        if (!started) {
            waitingForPanel = false
            listener.onError("屏幕参数查询发送失败")
        } else {
            listener.onState("读取屏幕参数中")
            armTimeout(5_000, "屏幕参数响应超时") { waitingForPanel = false }
        }
    }

    private fun acceptPanel(data: ByteArray) {
        if (!waitingForPanel || data.size != 9 || data[0].toInt() and 0xff != 0x55 ||
            data[1].toInt() and 0xff != 0x42 || data[2].toInt() and 0xff != 5) return
        val sum = data.dropLast(1).sumOf { it.toInt() and 0xff } and 0xff
        if (sum != (data[8].toInt() and 0xff)) return
        waitingForPanel = false
        clearTimeout()
        val panelId = data[3].toInt() and 0xff
        val width = ((data[4].toInt() and 0xff) shl 8) or (data[5].toInt() and 0xff)
        val height = ((data[6].toInt() and 0xff) shl 8) or (data[7].toInt() and 0xff)
        listener.onPanel(panelId, width, height)
    }

    private fun fail(message: String) {
        closeGatt()
        listener.onError(message)
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        generation++
        clearTimeout()
        waitingForPanel = false
        characteristic = null
        val old = gatt
        gatt = null
        try { old?.disconnect() } catch (_: SecurityException) { }
        old?.close()
    }

    fun close() = onMain {
        stopScan(false)
        closeGatt()
        listener.onState("Ark 蓝牙已关闭")
    }
}
