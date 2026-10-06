package org.framefilm.ark

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.WindowInsets
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** Manual diagnostic UI; the same controllers are callable from instrumentation. */
@Suppress("DEPRECATION") // API 29 system window inset fallback.
class MainActivity : Activity() {
    private lateinit var ble: ArkBleController
    private lateinit var p2p: ArkP2pController
    private var transfer: DirectTransferCoordinator? = null
    private lateinit var devicesView: LinearLayout
    private lateinit var bleStatus: TextView
    private lateinit var p2pStatus: TextView
    private lateinit var panelStatus: TextView
    private lateinit var logView: TextView
    private var devices: List<BluetoothDevice> = emptyList()
    private val lines = ArrayDeque<String>()
    private var pendingAction: (() -> Unit)? = null
    private var destroyed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ble = ArkBleController(this, object : ArkBleController.Listener {
            override fun onDevices(devices: List<BluetoothDevice>) {
                this@MainActivity.devices = devices
                showDevices()
            }
            override fun onReady() { ble.readPanel() }
            override fun onPanel(panelId: Int, width: Int, height: Int) {
                panelStatus.text = "屏幕：ID 0x${panelId.toString(16).uppercase()}，${width}×${height}"
                if (panelId == 0x02 && width == 720 && height == 480) append("Ark 屏幕参数核对通过")
                else append("屏幕参数与 Ark 预期不符")
            }
            override fun onState(state: String) {
                bleStatus.text = "蓝牙：$state"
                append(state)
                if (state.startsWith("扫描结束") && devices.size == 1) ble.connect(devices.single())
            }
            override fun onError(message: String) {
                bleStatus.text = "蓝牙：$message"
                append("蓝牙：$message")
            }
        })
        p2p = ArkP2pController(this, object : ArkP2pController.Listener {
            override fun onGroupReady(frequencyMhz: Int) {
                p2pStatus.text = "Wi-Fi Direct：2.4GHz GO，${frequencyMhz}MHz"
                append("2.4GHz 直连组已验收")
            }
            override fun onState(state: String) { p2pStatus.text = "Wi-Fi Direct：$state"; append(state) }
            override fun onError(message: String) {
                p2pStatus.text = "Wi-Fi Direct：$message"
                append("Wi-Fi Direct：$message")
            }
        })

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(12), dp(20), dp(24))
        }
        root.setOnApplyWindowInsetsListener { view, insets ->
            val top: Int
            val bottom: Int
            if (Build.VERSION.SDK_INT >= 30) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars())
                top = bars.top
                bottom = bars.bottom
            } else {
                top = insets.systemWindowInsetTop
                bottom = insets.systemWindowInsetBottom
            }
            view.setPadding(dp(20), dp(12) + top, dp(20), dp(24) + bottom)
            insets
        }
        fun label(text: String, size: Float = 17f): TextView = TextView(this).apply {
            this.text = text
            textSize = size
            setPadding(0, dp(8), 0, dp(8))
            root.addView(this)
        }
        fun button(text: String, action: () -> Unit) {
            root.addView(Button(this).apply {
                this.text = text
                minHeight = dp(48)
                setOnClickListener { action() }
            })
        }
        label("Ark 连接诊断", 25f)
        label("蓝牙诊断、2.4GHz 直连组和已知 film 文件直传。")
        bleStatus = label("蓝牙：未连接")
        panelStatus = label("屏幕：未读取")
        button("扫描并连接 Ark") { withPermissions(blePermissions()) { ble.startScan() } }
        devicesView = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(devicesView)
        button("重读屏幕参数") { withPermissions(blePermissions()) { ble.readPanel() } }
        button("断开蓝牙") { ble.close() }
        p2pStatus = label("Wi-Fi Direct：未创建")
        button("创建 2.4GHz 直连组") { withPermissions(wifiPermissions()) { p2p.createGroup2Ghz() } }
        button("释放本次直连组") { p2p.close() }
        button("WiFi 直传测试文件") {
            withPermissions(blePermissions() + wifiPermissions()) {
                if (transfer != null) { append("直传正在进行，请等待结束或取消"); return@withPermissions }
                ble.close()
                p2p.close { clean ->
                    if (!clean) { append("旧直连组清理未确认，请重试"); return@close }
                    transfer = DirectTransferCoordinator(this, object : DirectTransferCoordinator.Listener {
                        override fun onProgress(message: String) { append(message) }
                        override fun onFinished(success: Boolean, message: String, cleanupCompleted: Boolean) {
                            append("${if (success) "通过" else "失败"}：$message；直连组清理=${if (cleanupCompleted) "完成" else "未确认"}")
                            transfer = null
                        }
                    }).also { it.start() }
                }
            }
        }
        button("取消直传") { transfer?.cancel() }
        label("最近状态")
        logView = label("等待手动操作", 14f)
        setContentView(ScrollView(this).apply {
            isFillViewport = true
            addView(root)
        })
    }

    private fun showDevices() {
        devicesView.removeAllViews()
        if (devices.isEmpty()) {
            devicesView.addView(TextView(this).apply { text = "未发现 Ark，可重试扫描"; setPadding(0, dp(8), 0, dp(8)) })
        } else {
            devices.forEachIndexed { index, device ->
                devicesView.addView(Button(this).apply {
                    text = "连接 Ark ${index + 1}"
                    minHeight = dp(48)
                    setOnClickListener { withPermissions(blePermissions()) { ble.connect(device) } }
                })
            }
        }
    }

    private fun blePermissions(): Array<String> = if (Build.VERSION.SDK_INT >= 31) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun wifiPermissions(): Array<String> = if (Build.VERSION.SDK_INT >= 33) {
        arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
    } else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun withPermissions(required: Array<String>, action: () -> Unit) {
        val missing = required.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) action()
        else {
            pendingAction = action
            requestPermissions(missing.toTypedArray(), 100)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode != 100) return
        val action = pendingAction
        pendingAction = null
        if (results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) action?.invoke()
        else append("权限未授予，请在系统设置中允许附近设备后重试")
    }

    private fun append(message: String) {
        if (destroyed) return
        if (lines.size >= 30) lines.removeFirst()
        lines.addLast(message)
        if (::logView.isInitialized) logView.text = lines.joinToString("\n")
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

    override fun onDestroy() {
        destroyed = true
        pendingAction = null
        transfer?.cancel()
        ble.close()
        p2p.close()
        super.onDestroy()
    }
}
