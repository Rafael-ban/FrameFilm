package org.framefilm.framefilm_ark

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.provider.OpenableColumns
import java.io.File
import java.util.concurrent.Executors
import org.framefilm.ark.FilmConverter
import org.framefilm.ark.FirmwareDownloader
import org.framefilm.ark.ArkFirmware
import org.framefilm.ark.TransferSnapshot
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
    private val worker = Executors.newSingleThreadExecutor()
    private val cache = File(activity.cacheDir, "flutter-film").apply {
        mkdirs()
        // Once per process: a previous process cannot still serve these cached files.
        if (!cacheInitialized) {
            listFiles()?.filter { it.isFile && it.name.startsWith("import-") }?.forEach { it.delete() }
            cacheInitialized = true
        }
    }
    private var currentFile: File? = null
    private var importedName: String? = null
    private var firmwareFile: File? = null
    private var importedFirmware: ArkFirmware? = null
    private var firmwareName: String? = null
    private class Download(val downloader: FirmwareDownloader, val result: MethodChannel.Result) {
        @Volatile var file: File? = null
    }
    private var download: Download? = null
    private var firmwareDownload: Map<String, Any> = downloadState("idle", 0, 0, "", false)
    private var pickingFirmware = false
    private var importing = false
    private var importResult: MethodChannel.Result? = null
    private var transferPermission = false
    private var transfer = TransferSnapshot("idle", "请导入 film 文件", cleanupCompleted = true)
    private var connected = false
    private var message = "请扫描并连接 Ark"
    private var devices: List<Map<String, String>> = emptyList()
    private var name: String? = null
    private var battery: Int? = null
    private var width: Int? = null
    private var height: Int? = null
    private var autoSleep: Boolean? = null
    private var timedWake: Boolean? = null
    private var wakeMinutes: Int? = null
    private var syncedAt: Long? = null
    private var settingsMessage: String? = null
    private var permissionAction: (() -> Unit)? = null
    private var permissionResult: MethodChannel.Result? = null
    private var refreshResult: MethodChannel.Result? = null
    private var refreshing = false
    private var readIndex = 0
    private val channels = intArrayOf(0x42, 0x23, 0x54, 0x26, 0x28, 0x2A)
    private class Read(val channel: Int, val done: ((ByteArray) -> Unit)? = null) {
        var writeDone = false
        var response: ByteArray? = null
        var timeout: Runnable? = null
    }
    private var pending: Read? = null
    private class PassportJob(val result: MethodChannel.Result, val expected: String? = null) {
        val bytes = java.io.ByteArrayOutputStream()
        var total = -1
        var binDone = false
        var jsonDone = false
        var retries = 0
    }
    private var passportJob: PassportJob? = null
    private var passportJson: String? = null
    private var passportRevision = 0
    private var passportPhase = "idle"
    private var passportCompleted = 0
    private var passportTotal = 0
    private var passportMessage = ""
    private var avatarResult: MethodChannel.Result? = null
    private val drafts get() = activity.getSharedPreferences("ark-passport", Activity.MODE_PRIVATE)
    private fun passportState(): Map<String, Any?> = mapOf(
        "phase" to passportPhase, "completed" to passportCompleted, "total" to passportTotal,
        "message" to passportMessage, "canCancel" to (passportJob != null),
        "json" to passportJson, "revision" to passportRevision)

    private val session: ArkSession = ArkSession(activity, object : ArkSession.Listener {
        override fun onTransfer(snapshot: TransferSnapshot) {
            transfer = snapshot
            if (closed) {
                if (snapshot.phase in setOf("done", "cancelled", "error", "unconfirmed")) { deleteCurrent(); deleteFirmware() }
                return
            }
            emit()
        }
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
            if (becameReady && !session.transferActive) beginRefresh(null)
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
        "name" to name, "battery" to battery, "width" to width, "height" to height,
        "autoSleep" to autoSleep, "timedWake" to timedWake, "wakeMinutes" to wakeMinutes,
        "syncedAt" to syncedAt, "settingsMessage" to settingsMessage, "settingsBusy" to refreshing,
        "importing" to importing,
        "firmwareDownload" to firmwareDownload, "passport" to passportState(),
        "hasFirmwareState" to true,
        "importedFirmware" to importedFirmware?.let { mapOf("name" to firmwareName,
            "size" to it.size, "version" to it.version, "project" to it.project,
            "elfSha256" to it.elfSha256, "fileSha256" to it.fileSha256) },
        "importedFile" to currentFile?.let { mapOf("name" to importedName,
            "size" to it.length(), "width" to FilmConverter.WIDTH, "height" to FilmConverter.HEIGHT) },
        "transfer" to mapOf("phase" to transfer.phase, "message" to transfer.message,
            "received" to transfer.received, "total" to transfer.total,
            "canCancel" to transfer.canCancel, "canRetry" to transfer.canRetry,
            "kind" to transfer.kind, "targetVersion" to transfer.targetVersion,
            "targetBuild" to transfer.targetBuild, "canConfirm" to transfer.canConfirm,
            "success" to transfer.success, "cleanupCompleted" to transfer.cleanupCompleted))

    private fun emit() { if (!closed) sink?.success(snapshot()) }

    override fun onListen(arguments: Any?, eventSink: EventChannel.EventSink) {
        sink = eventSink
        emit()
    }

    override fun onCancel(arguments: Any?) { sink = null }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        if (closed) { result.error("closed", "蓝牙会话已关闭", null); return }
        try {
            if (call.method in setOf("scan", "connect", "refresh") && session.transferActive) {
                result.error("busy", "直传期间请等待传输结束", null)
                return
            }
            if (refreshing && call.method !in setOf("snapshot", "refresh", "disconnect", "cancelTransfer", "cancelFirmwareDownload", "cancelPassport", "loadPassportDraft", "savePassportDraft")) {
                result.error("busy", "设备读写正在进行，请稍后重试", null)
                return
            }
            when (call.method) {
                "loadPassportDraft" -> result.success(drafts.getString("json", null))
                "savePassportDraft" -> {
                    val json = requireNotNull(call.argument<String>("json"))
                    validatePassport(json)
                    check(drafts.edit().putString("json", json).commit()) { "草稿保存失败" }
                    result.success(null)
                }
                "pickPassportAvatar" -> {
                    checkReplace(); check(avatarResult == null) { "正在选择头像" }
                    avatarResult = result
                    try {
                        activity.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE); type = "image/*"
                        }, PICK_AVATAR)
                    } catch (error: Exception) { avatarResult = null; throw error }
                }
                "readPassport" -> beginPassport(result)
                "savePassport" -> savePassport(call, result)
                "cancelPassport" -> {
                    if (passportJob != null) {
                        finishPassport("cancelled", "操作已取消")
                        pending?.timeout?.let(main::removeCallbacks); pending = null
                        session.disconnect(); connected = false; clearDetails()
                    }
                    emit(); result.success(snapshot())
                }
                "remoteKey", "openDevicePage" -> remoteCommand(call, result)
                "setAutoSleep", "setTimedWake", "setWakeMinutes", "renameDevice", "syncTime" -> settingsCommand(call, result)
                "downloadFirmware" -> downloadFirmware(call, result)
                "cancelFirmwareDownload" -> {
                    cancelFirmwareDownload()
                    result.success(snapshot())
                }
                "pickFilm" -> pickFilm(result)
                "pickFirmware", "importFirmware" -> pickFilm(result, firmware = true)
                "clearFirmware" -> {
                    checkReplace()
                    deleteFirmware()
                    transfer = TransferSnapshot("idle", "已清除固件", cleanupCompleted = true, kind = "firmware")
                    emit(); result.success(snapshot())
                }
                "startFirmwareTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    val file = checkNotNull(firmwareFile) { "请先导入 app.bin 固件" }
                    val image = checkNotNull(importedFirmware)
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.startFirmwareTransfer(file, image)
                        result.success(snapshot())
                    }
                }
                "confirmFirmwareTransfer" -> withPermissions(result) {
                    session.confirmFirmwareTransfer()
                    result.success(snapshot())
                }
                "clearFilm" -> {
                    checkReplace()
                    deleteCurrent()
                    transfer = TransferSnapshot("idle", "已清除 film 文件", cleanupCompleted = true)
                    emit()
                    result.success(snapshot())
                }
                "retryTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.retryTransfer()
                        result.success(snapshot())
                    }
                }
                "startTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    val file = checkNotNull(currentFile) { "请先导入 film 文件" }
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.startTransfer(file, checkNotNull(importedName))
                        result.success(snapshot())
                    }
                }
                "cancelTransfer" -> {
                    session.cancelTransfer()
                    result.success(snapshot())
                }
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

    private fun withPermissions(result: MethodChannel.Result, direct: Boolean = false, action: () -> Unit) {
        val required = when {
            Build.VERSION.SDK_INT >= 31 -> arrayOf(Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT)
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val requested = if (!direct) required else required + when {
            Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = requested.filter { activity.checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) { action(); return }
        if (permissionResult != null) {
            result.error("busy", "请先完成蓝牙权限授权", null)
            return
        }
        transferPermission = direct
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
            message = "需要附近设备 / Wi-Fi 或定位权限，请允许后重试"
            if (transferPermission) transfer = TransferSnapshot("error", message,
                total = currentFile?.length() ?: 0, canRetry = currentFile != null,
                cleanupCompleted = transfer.cleanupCompleted)
            transferPermission = false
            emit()
            result.error("permission_denied", message, null)
        } else {
            try { action?.invoke() }
            catch (error: Exception) { result.error("operation_failed", error.message, null) }
        }
        return true
    }

    private fun beginRefresh(result: MethodChannel.Result?) {
        if (session.transferActive || importing || transfer.canConfirm || (transfer.canRetry && !transfer.cleanupCompleted)) {
            result?.error("busy", "请等待导入、传输和清理完成后读取设备信息", null)
            return
        }
        if (!connected || !session.isReady) {
            result?.error("not_connected", "请先连接 Ark", null)
            return
        }
        if (passportJob != null) {
            result?.error("busy", "通行证操作正在进行", null); return
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
        request(channels[readIndex])
    }

    private fun request(channel: Int, payload: ByteArray = byteArrayOf(), done: ((ByteArray) -> Unit)? = null) {
        val read = Read(channel, done)
        pending = read
        message = "正在读取设备信息"
        emit()
        read.timeout = Runnable {
            if (pending === read) failRead("timeout", "设备响应超时，请重新连接 Ark")
        }.also { main.postDelayed(it, 7_000) }
        val bytes = byteArrayOf(0x55, read.channel.toByte(), payload.size.toByte()) + payload
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
        if (read.done != null) {
            read.timeout?.let(main::removeCallbacks)
            pending = null
            try { read.done.invoke(data) }
            catch (error: Exception) { failRead("invalid_response", error.message ?: "设备返回格式无效") }
            return
        }
        val valid = when (read.channel) {
            0x42 -> data.size == 5 && u16(data, 1) > 0 && u16(data, 3) > 0
            0x23 -> data.size == 1 && (data[0].toInt() and 255) in 0..100
            0x54 -> data.size in 3..31 && data[0] == 0.toByte() && data.last() == 0.toByte()
            0x26, 0x28 -> data.size == 1 && data[0].toInt() in 0..1
            0x2A -> data.size == 2 && u16(data, 0) in 10..2880
            else -> false
        }
        if (!valid) { failRead("invalid_response", "设备返回的信息格式无效"); return }
        when (read.channel) {
            0x42 -> { width = u16(data, 1); height = u16(data, 3) }
            0x23 -> battery = data[0].toInt() and 255
            0x54 -> name = data.copyOfRange(1, data.lastIndex).toString(Charsets.UTF_8)
            0x26 -> autoSleep = data[0].toInt() == 1
            0x28 -> timedWake = data[0].toInt() == 1
            0x2A -> wakeMinutes = u16(data, 0)
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
        if (passportJob != null) finishPassport("error", detail)
        pending?.timeout?.let(main::removeCallbacks)
        pending = null
        refreshing = false
        val result = refreshResult
        refreshResult = null
        result?.error(code, detail, null)
    }

    private fun clearDetails() {
        name = null; battery = null; width = null; height = null
        autoSleep = null; timedWake = null; wakeMinutes = null; syncedAt = null; settingsMessage = null
    }

    private fun finishSettings(detail: String, failure: Boolean = false) {
        refreshing = false
        settingsMessage = detail
        val result = refreshResult
        refreshResult = null
        emit()
        if (failure) result?.error("settings_failed", detail, null) else result?.success(snapshot())
    }

    private fun settingsCommand(call: MethodCall, result: MethodChannel.Result) {
        check(connected && session.isReady) { "请先连接 Ark" }
        check(!refreshing && !importing && !session.transferActive && !transferPermission &&
            !transfer.canConfirm && (!transfer.canRetry || transfer.cleanupCompleted)) { "请等待当前操作或清理完成" }
        val channel: Int
        val payload: ByteArray
        var getChannel: Int? = null
        when (call.method) {
            "setAutoSleep", "setTimedWake" -> {
                check(if (call.method == "setAutoSleep") autoSleep != null else timedWake != null) { "请先读取设备设置" }
                val enabled = requireNotNull(call.argument<Boolean>("enabled")) { "缺少开关值" }
                channel = if (call.method == "setAutoSleep") 0x25 else 0x27
                getChannel = channel + 1
                payload = byteArrayOf(if (enabled) 1 else 0)
            }
            "setWakeMinutes" -> {
                check(wakeMinutes != null) { "请先读取设备设置" }
                val value = requireNotNull(call.argument<Number>("minutes")).toInt()
                require(value in 10..2880) { "唤醒间隔必须为 10–2880 分钟" }
                channel = 0x29; getChannel = 0x2A
                payload = byteArrayOf((value shr 8).toByte(), value.toByte())
            }
            "renameDevice" -> {
                check(name != null) { "请先读取设备名称" }
                val suffix = requireNotNull(call.argument<String>("suffix"))
                val bytes = suffix.toByteArray(Charsets.UTF_8)
                require(bytes.size in 1..16 && suffix.codePoints().allMatch {
                    it >= 0x20 && it !in 0x7F..0x9F && it !in 0xD800..0xDFFF
                }) { "名称后缀需为 1–16 UTF-8 字节，且不能包含控制字符" }
                channel = 0x55; payload = bytes + byteArrayOf(0)
            }
            else -> {
                val now = System.currentTimeMillis()
                val seconds = now / 1000
                val offset = java.util.TimeZone.getDefault().getOffset(now) / 60000
                require(seconds in 1577836800L..4102444800L && offset in -840..840) { "手机时间或时区超出设备范围" }
                channel = 0x4D
                payload = byteArrayOf((seconds shr 24).toByte(), (seconds shr 16).toByte(),
                    (seconds shr 8).toByte(), seconds.toByte(), (offset shr 8).toByte(), offset.toByte())
            }
        }
        refreshing = true
        refreshResult = result
        settingsMessage = "正在保存设备设置"
        if (getChannel != null) {
            val readChannel = getChannel
            // SET has no protocol ACK. Keep the same operation lock through GET verification.
            val marker = Read(channel)
            pending = marker
            marker.timeout = Runnable { if (pending === marker) failRead("timeout", "设备写入超时") }
                .also { main.postDelayed(it, 7_000) }
            val bytes = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
            session.writePacket(bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())) { outcome ->
                if (closed || pending !== marker) return@writePacket
                marker.timeout?.let(main::removeCallbacks)
                pending = null
                outcome.fold(onSuccess = {
                    request(readChannel) { data ->
                        require(if (readChannel == 0x2A) data.size == 2 && u16(data, 0) in 10..2880
                            else data.size == 1 && data[0].toInt() in 0..1) { "设备返回设置格式无效" }
                        when (readChannel) {
                            0x26 -> autoSleep = data[0].toInt() == 1
                            0x28 -> timedWake = data[0].toInt() == 1
                            0x2A -> wakeMinutes = u16(data, 0)
                        }
                        val matches = data.contentEquals(payload)
                        finishSettings(if (matches) "已保存并回读确认" else "保存后回读与请求不一致，请刷新后重试", !matches)
                    }
                }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
            }
        } else {
            request(channel, payload) { data ->
                if (channel == 0x55) {
                    if (data.size == 1 && data[0].toInt() in 1..2) {
                        finishSettings(if (data[0].toInt() == 1) "设备拒绝名称参数" else "设备名称存储失败", true)
                    } else {
                        require(data.size in 3..31 && data[0] == 0.toByte() && data.last() == 0.toByte()) { "名称回包无效" }
                        name = data.copyOfRange(1, data.lastIndex).toString(Charsets.UTF_8)
                        val expected = "FRAMEFILMARK-" + payload.copyOfRange(0, payload.lastIndex).toString(Charsets.UTF_8)
                        val matches = name == expected
                        finishSettings(if (matches) "已保存，设备重启后广播名生效" else "设备返回的名称与请求不一致", !matches)
                    }
                } else {
                    val matches = data.contentEquals(payload)
                    if (matches) syncedAt = System.currentTimeMillis()
                    finishSettings(if (matches) "时间同步完成" else "时间同步回显与请求不一致", !matches)
                }
            }
        }
        emit()
    }
    private fun validatePassport(raw: String) {
        require(raw.toByteArray(Charsets.UTF_8).size in 1..350000) { "资料大小无效" }
        val value = org.json.JSONObject(raw)
        require(value.get("version") is Number && (value.get("version") as Number).toDouble() == 1.0) { "资料版本不支持" }
        for ((field, limit) in mapOf("codename" to 32, "number" to 36, "affiliation" to 60, "signature" to 200)) {
            val text = value.get(field)
            require(text is String && text.length <= limit) { "$field 格式或长度无效" }
        }
        require(value.get("codenameUnset") is Boolean && value.get("numberUnset") is Boolean) { "未设置标记无效" }
        require(value.has("avatar") && (value.isNull("avatar") || value.get("avatar") is String)) { "头像字段无效" }
    }

    private fun checkPassportAvailable() {
        check(connected && session.isReady) { "请先连接 Ark" }
        checkReplace()
        check(!transferPermission && !transfer.canConfirm && (!transfer.canRetry || transfer.cleanupCompleted)) { "请等待传输清理完成" }
    }

    private fun beginPassport(result: MethodChannel.Result) {
        checkPassportAvailable()
        val job = PassportJob(result)
        passportJob = job; refreshing = true
        passportPhase = "reading"; passportCompleted = 0; passportTotal = 0
        passportMessage = "正在读取设备通行证资料"; emit()
        readPassportChunk(job)
    }

    private fun finishPassport(phase: String, detail: String) {
        val job = passportJob ?: return
        passportJob = null; refreshing = false; passportPhase = phase
        passportMessage = if (phase != "done" && job.expected != null) {
            val progress = when {
                job.jsonDone -> "两个文件已发送，但资料回读未确认，位图未校验。"
                job.binDone -> "profile.bin 已发送，profile.json 未确认完成。"
                else -> "profile.bin 未确认完成，profile.json 尚未发送。"
            }
            "$detail。$progress 请重新连接后从头重试。"
        } else if (phase != "done") "$detail；之前读取的资料保留。" else detail
        emit(); job.result.success(snapshot())
    }

    private fun be32(value: Int) = byteArrayOf((value ushr 24).toByte(), (value ushr 16).toByte(),
        (value ushr 8).toByte(), value.toByte())
    private fun u32(bytes: ByteArray, offset: Int): Long = (0..3).fold(0L) { value, i ->
        (value shl 8) or (bytes[offset + i].toLong() and 255) }
    private fun packet(channel: Int, payload: ByteArray): ByteArray {
        val bytes = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
        return bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())
    }

    private fun readPassportChunk(job: PassportJob) {
        if (passportJob !== job || closed) return
        val offset = job.bytes.size()
        request(0x53, be32(offset) + byteArrayOf(128.toByte())) { data ->
            if (passportJob !== job) return@request
            require(data.size in 9..137 && u32(data, 1) == offset.toLong()) { "资料响应位置或长度无效" }
            val status = data[0].toInt() and 255
            if (status == 3 && job.expected != null && job.retries++ < 12) {
                main.postDelayed({ if (passportJob === job) readPassportChunk(job) }, 500)
                return@request
            }
            if (status != 0) {
                finishPassport("error", when (status) { 1 -> "设备尚未配置资料"; 3 -> "设备忙，请稍后重试"; else -> "设备读取失败（状态 $status）" })
                return@request
            }
            job.retries = 0
            val total = u32(data, 5)
            require(total in 1..350000 && (job.total == -1 || job.total.toLong() == total)) { "资料长度变化或超出范围" }
            job.total = total.toInt()
            val count = data.size - 9
            require(offset + count <= job.total && (count > 0 || offset == job.total)) { "资料分块不完整" }
            job.bytes.write(data, 9, count)
            if (job.expected == null) { passportCompleted = job.bytes.size(); passportTotal = job.total }
            emit()
            if (job.bytes.size() == job.total) {
                val raw = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(job.bytes.toByteArray())).toString()
                validatePassport(raw)
                if (job.expected != null && raw != job.expected) {
                    finishPassport("error", "资料回读与发送内容不一致")
                } else {
                    passportJson = raw; passportRevision++
                    finishPassport("done", if (job.expected == null) "设备资料读取完成" else
                        "资料回读一致；屏幕位图仍待实体确认。请回到通行证页或按确认刷新。")
                }
            } else main.postDelayed({ if (passportJob === job) readPassportChunk(job) }, 50)
        }
    }

    private fun savePassport(call: MethodCall, result: MethodChannel.Result) {
        checkPassportAvailable()
        val raw = requireNotNull(call.argument<String>("json")) { "缺少资料 JSON" }
        validatePassport(raw)
        val bin = requireNotNull(call.argument<ByteArray>("bin")) { "缺少位图" }
        require(bin.size == 33456 && bin.copyOfRange(0, 6).contentEquals(byteArrayOf(70,70,85,73,1,1)) &&
            (bin[6].toInt() and 255) == 184 && (bin[7].toInt() and 255) == 1 &&
            (bin[8].toInt() and 255) == 96 && (bin[9].toInt() and 255) == 2 &&
            bin.copyOfRange(10,16).all { it == 0.toByte() }) { "位图必须为 FFUI v1 440×608 单色文件（33456B）" }
        val json = raw.toByteArray(Charsets.UTF_8)
        val job = PassportJob(result, raw)
        passportJob = job; refreshing = true; passportPhase = "sending"
        passportCompleted = 0; passportTotal = bin.size + json.size
        passportMessage = "正在发送 profile.bin"; emit()
        uploadPassportFile(job, "app/pass/profile.bin", bin, 0) {
            job.binDone = true; passportMessage = "profile.bin 已发送，正在发送 profile.json"; emit()
            main.postDelayed({ if (passportJob === job) uploadPassportFile(job, "app/pass/profile.json", json, bin.size) {
                job.jsonDone = true; passportPhase = "verifying"
                passportMessage = "文件已发送，正在回读资料；协议无保存 ACK"; emit()
                main.postDelayed({ if (passportJob === job) readPassportChunk(job) }, 750)
            } }, 750)
        }
    }

    private fun passportWrite(job: PassportJob, channel: Int, payload: ByteArray, next: () -> Unit) {
        if (passportJob !== job || closed) return
        val marker = Read(channel); pending = marker
        marker.timeout = Runnable { if (pending === marker) failRead("timeout", "蓝牙写入超时") }
            .also { main.postDelayed(it, 7000) }
        session.writePacket(packet(channel, payload)) { outcome ->
            if (passportJob !== job || pending !== marker || closed) return@writePacket
            marker.timeout?.let(main::removeCallbacks); pending = null
            outcome.fold(onSuccess = {
                main.postDelayed({ if (passportJob === job) next() }, if (channel == 0x02) 4L else 50L)
            }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
        }
    }

    private fun uploadPassportFile(job: PassportJob, path: String, bytes: ByteArray, base: Int, done: () -> Unit) {
        fun chunk(offset: Int) {
            if (offset == bytes.size) {
                passportWrite(job, 0x04, byteArrayOf(), done); return
            }
            val end = minOf(offset + 192, bytes.size)
            passportWrite(job, 0x02, bytes.copyOfRange(offset, end)) {
                passportCompleted = base + end; emit(); chunk(end)
            }
        }
        passportWrite(job, 0x03, byteArrayOf()) {
            passportWrite(job, 0x00, path.toByteArray(Charsets.US_ASCII) + byteArrayOf(0)) {
                passportWrite(job, 0x01, be32(bytes.size)) { chunk(0) }
            }
        }
    }

    private fun remoteCommand(call: MethodCall, result: MethodChannel.Result) {
        checkPassportAvailable()
        val remote = call.method == "remoteKey"
        val value = requireNotNull(call.argument<Number>(if (remote) "key" else "appId")).toInt()
        require(value in if (remote) 0..4 else 0..6) { "不支持的按键或页面 ID" }
        refreshing = true; refreshResult = result
        if (remote) request(0x4E, byteArrayOf(value.toByte())) { data ->
            require(data.contentEquals(byteArrayOf(value.toByte()))) { "遥控按键回显不一致" }
            finishSettings("设备已收到按键；开机或休眠页面可能忽略操作")
        } else {
            val marker = Read(0x4B); pending = marker
            marker.timeout = Runnable { if (pending === marker) failRead("timeout", "页面切换写入超时") }
                .also { main.postDelayed(it, 7000) }
            session.writePacket(packet(0x4B, byteArrayOf(value.toByte()))) { outcome ->
                if (pending !== marker || closed) return@writePacket
                marker.timeout?.let(main::removeCallbacks)
                outcome.fold(onSuccess = {
                    main.postDelayed({
                        if (pending === marker && connected) {
                            pending = null
                            request(0x4C) { data ->
                                require(data.size == 1) { "当前页面响应无效" }
                                val matches = (data[0].toInt() and 255) == value
                                finishSettings(if (matches) "设备当前页面已确认" else "设备未切换到请求页面，可能未注册或正在忙", !matches)
                            }
                        }
                    }, 300)
                }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
            }
        }
        emit()
    }

    private fun u16(data: ByteArray, offset: Int) =
        ((data[offset].toInt() and 255) shl 8) or (data[offset + 1].toInt() and 255)

    private fun checkReplace() {
        check(!importing && !refreshing && !session.transferActive && !transferPermission &&
            transfer.cleanupCompleted && !transfer.canConfirm) { "请等待导入或直传清理结束" }
    }

    private fun deleteCurrent() {
        currentFile?.delete()
        currentFile = null
        importedName = null
    }

    private fun deleteFirmware() {
        firmwareFile?.delete()
        firmwareFile = null
        importedFirmware = null
        firmwareName = null
    }

    private fun downloadState(phase: String, received: Long, total: Long, detail: String,
                              canCancel: Boolean): Map<String, Any> = mapOf(
        "phase" to phase, "received" to received, "total" to total,
        "message" to detail, "canCancel" to canCancel)

    private fun replaceFirmware(file: File, image: ArkFirmware, displayName: String) {
        val previous = firmwareFile
        firmwareFile = file
        importedFirmware = image
        firmwareName = displayName
        if (previous != file) previous?.delete()
    }

    private fun cancelFirmwareDownload() {
        val job = download ?: return
        download = null
        job.downloader.cancel()
        job.file?.delete()
        importing = false
        firmwareDownload = downloadState("cancelled",
            firmwareDownload["received"] as Long, firmwareDownload["total"] as Long, "固件下载已取消", false)
        emit()
        job.result.success(snapshot())
    }

    private fun downloadFirmware(call: MethodCall, result: MethodChannel.Result) {
        checkReplace()
        val url = requireNotNull(call.argument<String>("url")) { "缺少固件下载地址" }
        val displayName = call.argument<String>("name")
        require(displayName == "frame_film_ark.bin") { "请选择原始 frame_film_ark.bin 资产" }
        val target = java.net.URI(url)
        require(target.scheme == "https" && target.host.equals("github.com", true) &&
            target.rawPath?.startsWith("/Rafael-ban/FrameFilm/releases/download/") == true &&
            target.rawPath?.endsWith("/frame_film_ark.bin") == true) { "固件必须来自 Rafael-ban/FrameFilm Release" }
        val size = requireNotNull(call.argument<Number>("size")) { "缺少固件大小" }.toLong()
        require(size in 1..FirmwareDownloader.MAX_SIZE) { "固件大小必须在 1 B 至 4 MiB 之间" }
        val sha256 = call.argument<String>("sha256")
        require(sha256 == null || sha256.matches(Regex("[a-fA-F0-9]{64}"))) { "固件 SHA256 格式无效" }
        val job = Download(FirmwareDownloader(), result)
        download = job
        importing = true
        firmwareDownload = downloadState("downloading", 0, size, "正在从 GitHub 下载固件", true)
        emit()
        worker.execute {
            var temporary: File? = null
            try {
                job.downloader.checkCancelled()
                val file = File.createTempFile("import-", ".bin", cache)
                temporary = file
                job.file = file
                var lastProgress = 0L
                job.downloader.download(url, file, size, sha256) { received ->
                    val now = android.os.SystemClock.elapsedRealtime()
                    if (received == size || now - lastProgress >= 150) {
                        lastProgress = now
                        main.post {
                            if (!closed && download === job) {
                                firmwareDownload = downloadState("downloading", received, size, "正在从 GitHub 下载固件", true)
                                emit()
                            }
                        }
                    }
                }
                main.post {
                    if (!closed && download === job) {
                        firmwareDownload = downloadState("validating", size, size, "正在校验 Ark 固件", true)
                        emit()
                    }
                }
                val image = ArkFirmware.inspect(file)
                job.downloader.checkCancelled()
                main.post {
                    if (closed || download !== job) file.delete()
                    else {
                        replaceFirmware(file, image, "frame_film_ark.bin")
                        download = null
                        importing = false
                        firmwareDownload = downloadState("done", size, size, "固件下载并校验完成", false)
                        transfer = TransferSnapshot("idle", "固件已导入：${image.version}", cleanupCompleted = true,
                            kind = "firmware", targetVersion = image.version, targetBuild = image.elfSha256)
                        emit()
                        result.success(snapshot())
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post {
                    if (!closed && download === job) {
                        download = null
                        importing = false
                        firmwareDownload = downloadState("error", firmwareDownload["received"] as Long,
                            size, error.message ?: "固件下载失败", false)
                        emit()
                        result.success(snapshot())
                    }
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun pickFilm(result: MethodChannel.Result, firmware: Boolean = false) {
        checkReplace()
        pickingFirmware = firmware
        importing = true
        importResult = result
        emit()
        try {
            activity.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
            }, PICK_FILM)
        } catch (error: Exception) {
            importing = false
            importResult = null
            emit()
            throw error
        }
    }

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode == PICK_AVATAR) {
            val result = avatarResult ?: return true
            avatarResult = null
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null) { result.success(null); return true }
            worker.execute {
                try {
                    val bytes = checkNotNull(activity.contentResolver.openInputStream(uri)).use { input ->
                        val output = java.io.ByteArrayOutputStream()
                        val buffer = ByteArray(8192)
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            require(output.size() + count <= 8 * 1024 * 1024) { "头像文件超过 8 MiB" }
                            output.write(buffer, 0, count)
                        }
                        output.toByteArray()
                    }
                    main.post { if (!closed) result.success(bytes) else result.error("closed", "会话已关闭", null) }
                } catch (error: Exception) { main.post { result.error("avatar_failed", error.message, null) } }
            }
            return true
        }
        if (requestCode != PICK_FILM) return false
        if (closed) return true
        val result = importResult
        val uri = data?.data
        if (resultCode != Activity.RESULT_OK || uri == null) {
            importing = false
            importResult = null
            emit()
            result?.success(snapshot())
            return true
        }
        val firmware = pickingFirmware
        worker.execute {
            var temporary: File? = null
            try {
                val displayName = activity.contentResolver.query(uri,
                    arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) it.getString(0) else null
                } ?: if (firmware) "app.bin" else "import.film"
                val file = File.createTempFile("import-", if (firmware) ".bin" else ".film", cache)
                temporary = file
                checkNotNull(activity.contentResolver.openInputStream(uri)).use { input ->
                    file.outputStream().use { output ->
                        val buffer = ByteArray(8192)
                        var total = 0L
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            if (Thread.currentThread().isInterrupted) error("导入已取消")
                            total += count
                            val limit = if (firmware) 4L * 1024 * 1024 else 32L * 1024 * 1024
                            require(total <= limit) { if (firmware) "固件超过 Ark 4 MiB Flash 容量" else "film 超过 32 MiB" }
                            output.write(buffer, 0, count)
                        }
                    }
                }
                val image = if (firmware) ArkFirmware.inspect(file) else null
                if (!firmware) FilmConverter.validate(file)
                val sendName = if (firmware) displayName else org.framefilm.ark.FilmTransferName.forFile(displayName, file)
                main.post {
                    if (closed) file.delete()
                    else {
                        if (firmware) {
                            replaceFirmware(file, checkNotNull(image), sendName)
                        } else {
                            deleteCurrent()
                            currentFile = file
                            importedName = sendName
                        }
                        importing = false
                        importResult = null
                        transfer = TransferSnapshot("idle", if (firmware) "固件已导入：${image?.version}" else "film 文件已导入，发送名 $sendName",
                            cleanupCompleted = true, kind = if (firmware) "firmware" else "film",
                            targetVersion = image?.version, targetBuild = image?.elfSha256)
                        emit()
                        result?.success(snapshot())
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post {
                    if (!closed) {
                        importing = false
                        importResult = null
                        message = error.message ?: if (firmware) "固件导入失败" else "film 导入失败"
                        emit()
                        result?.error("import_failed", message, null)
                    }
                }
            }
        }
        return true
    }

    fun close() {
        if (closed) return
        closed = true
        download?.let {
            it.downloader.cancel()
            it.file?.delete()
            it.result.error("closed", "会话已关闭", null)
        }
        download = null
        avatarResult?.error("closed", "会话已关闭", null); avatarResult = null
        clearRead("closed", "蓝牙会话已关闭")
        permissionResult?.error("closed", "蓝牙会话已关闭", null)
        permissionResult = null
        permissionAction = null
        methods.setMethodCallHandler(null)
        events.setStreamHandler(null)
        sink = null
        importResult?.error("closed", "会话已关闭", null)
        importResult = null
        worker.shutdownNow()
        val active = session.transferActive
        session.close()
        if (!active) { deleteCurrent(); deleteFirmware() }
    }

    private companion object { const val PERMISSION_REQUEST = 4207; const val PICK_FILM = 4208; const val PICK_AVATAR = 4209; var cacheInitialized = false }
}
