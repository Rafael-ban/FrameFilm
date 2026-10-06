package org.framefilm.ark

import android.app.Activity
import android.app.Instrumentation
import android.annotation.SuppressLint
import android.content.Context
import android.net.wifi.p2p.WifiP2pManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Debug hardware probe using the visible app's transfer coordinator. */
class DirectTransferProbe : Instrumentation() {
    private var cleanupGroup: String? = null
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        cleanupGroup = arguments?.getString("cleanupGroup")
        start()
    }

    override fun onStart() {
        cleanupGroup?.let { cleanupOnly(it); return }
        val done = CountDownLatch(1)
        var coordinator: DirectTransferCoordinator? = null
        var success = false
        var cleaned = false
        var detail = "直传探针超时"
        runOnMainSync {
            coordinator = DirectTransferCoordinator(targetContext, object : DirectTransferCoordinator.Listener {
                override fun onProgress(message: String) {
                    Log.i("ArkDirectProbe", message)
                    sendStatus(0, Bundle().apply { putString("progress", message) })
                }
                override fun onFinished(passed: Boolean, message: String, cleanupCompleted: Boolean) {
                    success = passed
                    cleaned = cleanupCompleted
                    detail = message
                    done.countDown()
                }
            }).also { it.start() }
        }
        if (!done.await(235, TimeUnit.SECONDS)) {
            runOnMainSync { coordinator?.cancel() }
            done.await(50, TimeUnit.SECONDS)
            success = false
            detail = "直传超时；$detail"
        }
        finish(if (success && cleaned) Activity.RESULT_OK else Activity.RESULT_CANCELED, Bundle().apply {
            putString("result", if (success && cleaned) "PASS" else "FAIL")
            putString("detail", detail)
            putBoolean("cleanupCompleted", cleaned)
        })
    }

    /** Recover one named GO group left by an interrupted probe. Never touches another group. */
    @SuppressLint("MissingPermission")
    private fun cleanupOnly(expectedName: String) {
        val done = CountDownLatch(1)
        val manager = targetContext.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
        var channel: WifiP2pManager.Channel? = null
        var cleaned = false
        var detail = "清理超时"
        var finished = false
        val main = Handler(Looper.getMainLooper())
        fun finishCleanup(success: Boolean, message: String) {
            if (finished) return
            finished = true
            cleaned = success
            detail = message
            done.countDown()
        }
        runOnMainSync {
            if (manager == null || expectedName.isBlank()) {
                finishCleanup(false, "无效直连组名称或设备不支持 P2P")
                return@runOnMainSync
            }
            channel = manager.initialize(targetContext, Looper.getMainLooper()) {
                finishCleanup(false, "P2P 通道断开")
            }
            val ch = channel
            if (ch == null) { finishCleanup(false, "无法初始化 P2P 通道"); return@runOnMainSync }
            fun remove(attempt: Int) {
                if (finished) return
                manager.requestGroupInfo(ch) { group ->
                    if (finished) return@requestGroupInfo
                    if (group == null) {
                        finishCleanup(true, "指定直连组已不存在")
                    } else if (group.networkName != expectedName || !group.isGroupOwner) {
                        finishCleanup(false, "当前组与指定名称或 GO 身份不匹配，未移除")
                    } else if (attempt >= 4) {
                        finishCleanup(false, "指定组仍存在，已停止重试")
                    } else {
                        manager.removeGroup(ch, object : WifiP2pManager.ActionListener {
                            override fun onSuccess() {
                                main.postDelayed({ remove(attempt + 1) }, 700)
                            }
                            override fun onFailure(reason: Int) {
                                if (attempt < 3) main.postDelayed({ remove(attempt + 1) }, 700)
                                else finishCleanup(false, "移除指定组失败，代码 $reason")
                            }
                        })
                    }
                }
            }
            remove(0)
        }
        if (!done.await(30, TimeUnit.SECONDS)) {
            runOnMainSync { finishCleanup(false, "移除指定组超时") }
        }
        runOnMainSync { channel?.close() }
        finish(if (cleaned) Activity.RESULT_OK else Activity.RESULT_CANCELED, Bundle().apply {
            putString("result", if (cleaned) "PASS" else "FAIL")
            putString("detail", detail)
            putBoolean("cleanupCompleted", cleaned)
        })
    }
}
