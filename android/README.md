# Ark Android 客户端

当前是 M0 通信验证版，用于在真实 Android 手机上验证 BLE 和 WiFi Direct 群组能力，为后续原生客户端建立基础。

## 当前范围

- 扫描并连接 `FRAMEFILMARK`，订阅 GATT 通知，通过 `0x42` 读取屏幕参数。
- 手机创建一个临时的 2.4GHz WiFi Direct 群组，检查群组拥有者身份和实际频率。
- 开发版自动探针在建组前后各读取一次 Ark 屏幕参数，然后释放自己创建的连接和群组。
- 不向 Ark 写入 WiFi 配置，不上传文件，不改设备参数。手机建组成功不代表 Ark 已加入群组，也不代表文件直传已经完成。

## 构建与安装

需要 JDK 17 或 21、Android SDK Platform 35 和 Build Tools 35.0.0。设置 `ANDROID_HOME`，或在不入库的 `local.properties` 中设置 `sdk.dir`。

```powershell
.\gradlew.bat :app:assembleDebug
adb -s <手机序列号> install -r -g app/build/outputs/apk/debug/app-debug.apk
adb -s <手机序列号> shell am start -n org.framefilm.ark.dev/org.framefilm.ark.MainActivity
```

开发版包名为 `org.framefilm.ark.dev`，与未来正式版分开。应用仅在用户操作后扫描、连接或建组。Android 12+ 需要附近蓝牙权限，Android 13+ 需要附近 WiFi 权限；较旧系统的扫描还需要定位权限和系统定位开关。

## 最小实机探针

先断开电脑网页与 Ark 的 BLE 连接，手机保持蓝牙和 WiFi 开启。不要在已有其他 WiFi Direct 业务运行时测试。探针拒绝接管已有群组。

```powershell
adb -s <手机序列号> shell am instrument -w org.framefilm.ark.dev/org.framefilm.ark.ConnectivityProbe
```

探针必须同时报告 `result=PASS` 和 `cleanupCompleted=true` 才算通过。它调用与界面相同的控制器，不模拟屏幕点击。探针仅编入 debug APK；输出不包含群组密码。

## 后续阶段

1. Ark 增加保存在 RAM 中的临时 WiFi 会话，手机通过 BLE 下发本次群组连接信息，避免覆盖已有 NVS 配网参数。
2. 验证 Ark 加入手机群组并通过 WiFi 获取一份测试 `.film`，以设备实际保存成功验收。
3. 接入有界缓冲写 SD、取消与超时清理，再逐步迁移网页中的图片转换、文件管理和设备设置。

第三方 ROM 声明支持 WiFi Direct 仅表示具备接口，兼容性以具体手机的实测结果为准。

## 当前验证记录（2026-10-07）

### 新手机：M0 通过

- 型号 `25042PN24C`、设备代号 `dijun`，系统自报 Android 17 / API 37。使用现有 APK 和已授予的附近设备权限执行探针。
- 成功扫描、连接 Ark，发现 GATT 服务并订阅通知，读取到面板 ID `0x02`、`720×480`；手机建立临时 2.4GHz GO 组后，BLE 再次查询仍得到相同屏参。
- 初测 GO 频率为 2462MHz，释放后系统状态回到 `InactiveState`，但客户端收到关闭通道的迟到回调，误报“清理未确认”。已修复：关闭前作废通道身份，忽略主动关闭及旧通道的迟到回调。
- 修复版构建、安装成功；仅对上述流程复测一次，GO 频率为 2412MHz，返回 `result=PASS`、`cleanupCompleted=true`，没有清理误报。修复版已安装到新手机。
- 复测日志：本地 `.output/ark/logs/android-m0-probe-dijun-fixed.log`。本轮只验证手机建组与 BLE 控制通信并存；Ark 尚未通过 WiFi 加入该组，没有进行 WiFi 文件传输或吞吐测试。

### 旧测试手机：小米 9 的安装恢复与 BLE 阻塞

- 空工程和 M0 业务代码的 `:app:assembleDebug` 均构建通过；使用 JDK 21、Gradle 8.11.1、AGP 8.10.1、SDK 35。
- 小米 9（第三方 HyperOS 2 / Android 15、API 35）的 USB 调试已授权，ADB 连接正常。
- 初次流式、非流式 ADB 安装以及手机本地安装都卡在已提交阶段。两个 ADB 会话虽接受取消请求，仍保留占用标记；此前记录的“已清理”不代表系统实际完成释放。
- APK 的 v2 签名校验通过，手机 Download 中的文件 SHA-256 与本地构建产物一致，手机剩余存储约 90GB。没有发现文件损坏、签名或存储不足问题。
- 重启手机并解锁后，**原 APK 未经修改即安装成功**，ADB 命令约 2 秒完成。安装阻塞随系统运行状态恢复而解除，具体卡住的系统组件尚未确定；没有为此修改应用代码或降低 targetSdk。
- 客户端主界面启动命令返回 `Status: ok`，冷启动约 696ms，应用进程存在。这是启动验证，不代表界面视觉验收。
- 首次通信探针因蓝牙运行时权限未就绪而退出，连接资源清理成功。随后确认 BLE 权限已授予，并补齐附近 WiFi 设备权限。
- 补齐权限后的探针已扫描到 Ark 并发起连接，随后返回 `Process crashed`；同一阶段日志记录系统进程 `com.android.bluetooth` 在 `MiuiBluetooth/system/gd/hci/hci_layer.cc:256` 因 `LE_EXTENDED_CREATE_CONNECTION(0x2043) was not expecting complete event` 中止。尚无成功的屏参回包，未进入 WiFi 建组步骤；不能把它记为 BLE 或双通道通过，也尚不能仅凭此断言探针退出的完整因果链。
- 本次安装问题已恢复；第三方 ROM 的 BLE 连接兼容性仍待单独定位。调试证据保存在本地 `.output/ark/logs/android-m0-probe.log` 和 `android-m0-probe-crash.log`，不将手机原始系统日志入库。
