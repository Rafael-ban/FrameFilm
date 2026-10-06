package org.framefilm.ark

import android.Manifest
import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pGroup
import android.net.wifi.p2p.WifiP2pManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.security.SecureRandom

/** Creates only a temporary 2.4 GHz group owned by this controller session. */
class ArkP2pController(context: Context, private val listener: Listener) {
    interface Listener {
        fun onGroupReady(frequencyMhz: Int)
        fun onState(state: String)
        fun onError(message: String)
    }

    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val manager = appContext.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private var channel: WifiP2pManager.Channel? = null
    private var channelGeneration = 0
    private var receiver: BroadcastReceiver? = null
    private var sessionName: String? = null
    private var generation = 0
    private var checking = false
    private var creating = false
    private var owned = false
    private var closing = false
    private var removing = false
    private var timeout: Runnable? = null
    private val closeCallbacks = mutableListOf<(Boolean) -> Unit>()
    private val random = SecureRandom()

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun hasPermission(): Boolean = if (Build.VERSION.SDK_INT >= 33) {
        appContext.checkSelfPermission(Manifest.permission.NEARBY_WIFI_DEVICES) == PackageManager.PERMISSION_GRANTED
    } else {
        appContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
    }

    private fun clearTimeout() {
        timeout?.let(main::removeCallbacks)
        timeout = null
    }

    private fun setTimeout(ms: Long, action: () -> Unit) {
        clearTimeout()
        timeout = Runnable { timeout = null; action() }.also { main.postDelayed(it, ms) }
    }

    private fun ensureChannel(): WifiP2pManager.Channel? {
        if (channel != null) return channel
        val p2p = manager ?: return null
        val token = ++channelGeneration
        channel = p2p.initialize(appContext, Looper.getMainLooper()) {
            onMain {
                if (token != channelGeneration || channel == null) return@onMain
                channel = null
                if (checking || creating || owned) listener.onError("Wi-Fi Direct 通道已断开")
                finishClose(false)
            }
        }
        if (receiver == null) {
            receiver = object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    if (intent.action == WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION &&
                        (creating || closing)) requestGroup()
                }
            }.also {
                val filter = IntentFilter(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                if (Build.VERSION.SDK_INT >= 33) appContext.registerReceiver(it, filter, Context.RECEIVER_EXPORTED)
                else appContext.registerReceiver(it, filter)
            }
        }
        return channel
    }

    @SuppressLint("MissingPermission")
    fun createGroup2Ghz() = onMain {
        if (!hasPermission()) { listener.onError("需要附近 Wi-Fi 设备权限"); return@onMain }
        if (checking || creating || owned || closing) { listener.onError("直连组操作正在进行"); return@onMain }
        val p2p = manager
        val ch = ensureChannel()
        if (p2p == null || ch == null) { listener.onError("设备不支持 Wi-Fi Direct"); return@onMain }
        checking = true
        val token = ++generation
        listener.onState("检查现有直连组")
        try {
            p2p.requestGroupInfo(ch) { existing -> onMain {
                if (token != generation || !checking || closing) return@onMain
                checking = false
                if (existing != null) {
                    listener.onError("已有直连组，未修改该组")
                    return@onMain
                }
                val alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                fun token(length: Int): String = buildString {
                    repeat(length) { append(alphabet[random.nextInt(alphabet.length)]) }
                }
                val name = "DIRECT-${token(2)}-${token(8)}"
                val config = WifiP2pConfig.Builder()
                    .setNetworkName(name)
                    .setPassphrase(token(16))
                    .setGroupOperatingBand(WifiP2pConfig.GROUP_OWNER_BAND_2GHZ)
                    .build()
                sessionName = name
                creating = true
                listener.onState("创建 2.4GHz 直连组中")
                setTimeout(20_000) {
                    if (creating) {
                        listener.onError("创建直连组超时，正在检查并清理")
                        closing = true
                        requestGroup()
                        setTimeout(5_000) { finishClose(false) }
                    }
                }
                try {
                    p2p.createGroup(ch, config, object : WifiP2pManager.ActionListener {
                        override fun onSuccess() { onMain { if (token == generation) requestGroup() } }
                        override fun onFailure(reason: Int) = onMain {
                            if (token != generation) return@onMain
                            if (closing) requestGroup()
                            else {
                                creating = false
                                sessionName = null
                                clearTimeout()
                                listener.onError("创建直连组失败：$reason")
                            }
                        }
                    })
                } catch (_: SecurityException) {
                    creating = false
                    sessionName = null
                    clearTimeout()
                    listener.onError("Wi-Fi Direct 权限不可用")
                }
            } }
        } catch (_: SecurityException) {
            checking = false
            listener.onError("Wi-Fi Direct 权限不可用")
        }
    }

    @SuppressLint("MissingPermission")
    private fun requestGroup() {
        val p2p = manager ?: return
        val ch = channel ?: return
        val token = generation
        try {
            p2p.requestGroupInfo(ch) { group -> onMain { if (token == generation) handleGroup(group) } }
        } catch (_: SecurityException) { listener.onError("无法查询直连组") }
    }

    private fun handleGroup(group: WifiP2pGroup?) {
        val name = sessionName ?: return
        if (group == null) {
            if (closing && !creating && !removing) finishClose(true)
            return
        }
        if (group.networkName != name) {
            if (closing) finishClose(false)
            else listener.onError("出现其他直连组，未修改该组")
            return
        }
        if (owned && !closing) return
        owned = true
        creating = false
        if (closing) { removeOwnedGroup(); return }
        val frequency = if (Build.VERSION.SDK_INT >= 29) group.frequency else 0
        if (!group.isGroupOwner || frequency !in 2400..2500) {
            listener.onError("直连组不是 2.4GHz Group Owner，正在释放")
            closing = true
            removeOwnedGroup()
            return
        }
        clearTimeout()
        listener.onState("2.4GHz 直连组已就绪")
        listener.onGroupReady(frequency)
    }

    @SuppressLint("MissingPermission")
    private fun removeOwnedGroup() {
        val p2p = manager
        val ch = channel
        val name = sessionName
        if (!owned || p2p == null || ch == null || name == null) { finishClose(false); return }
        if (removing) return
        removing = true
        val token = generation
        try {
            p2p.requestGroupInfo(ch) { group -> onMain {
                if (token != generation) return@onMain
                if (group?.networkName != name) {
                    finishClose(true)
                } else {
                    removeVerifiedGroup(p2p, ch)
                }
            } }
        } catch (_: SecurityException) {
            listener.onError("无法确认本次直连组：权限不可用")
            finishClose(false)
        }
    }

    @SuppressLint("MissingPermission")
    private fun removeVerifiedGroup(p2p: WifiP2pManager, ch: WifiP2pManager.Channel) {
        listener.onState("释放本次创建的直连组中")
        val token = generation
        try {
            p2p.removeGroup(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() = onMain { if (token == generation) finishClose(true) }
                override fun onFailure(reason: Int) = onMain {
                    if (token != generation) return@onMain
                    listener.onError("释放直连组失败：$reason")
                    finishClose(false)
                }
            })
            setTimeout(8_000) {
                if (token != generation) return@setTimeout
                listener.onError("释放直连组超时")
                finishClose(false)
            }
        } catch (_: SecurityException) {
            listener.onError("无法释放直连组：权限不可用")
            finishClose(false)
        }
    }

    fun close(onComplete: (Boolean) -> Unit = {}) = onMain {
        closeCallbacks += onComplete
        if (closing) return@onMain
        closing = true
        clearTimeout()
        if (owned) removeOwnedGroup()
        else if (creating) {
            requestGroup()
            setTimeout(20_000) {
                requestGroup()
                setTimeout(3_000) {
                    listener.onError("无法确认本次创建的直连组已清理")
                    finishClose(false)
                }
            }
        } else finishClose(true)
    }

    private fun finishClose(success: Boolean) {
        generation++
        clearTimeout()
        receiver?.let { try { appContext.unregisterReceiver(it) } catch (_: IllegalArgumentException) { } }
        receiver = null
        val oldChannel = channel
        channel = null
        channelGeneration++ // 先作废，再 close；迟到回调不得覆盖本次清理结果或新通道。
        oldChannel?.close()
        sessionName = null
        checking = false
        creating = false
        owned = false
        closing = false
        removing = false
        listener.onState(if (success) "Wi-Fi Direct 已关闭" else "Wi-Fi Direct 清理未确认")
        val callbacks = closeCallbacks.toList()
        closeCallbacks.clear()
        callbacks.forEach { it(success) }
    }
}
