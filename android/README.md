# Ark Android 客户端

当前是 M1 直传诊断版，使用 BLE 命令让 Ark 加入手机创建的临时 2.4GHz WiFi Direct 群组，从手机 HTTP 服务获取一份已知有效的 `.film`。

## 当前范围

- 扫描并连接 `FRAMEFILMARK`，订阅 GATT 通知，通过 `0x42` 读取屏幕参数。
- 手机创建一个临时的 2.4GHz WiFi Direct 群组，检查群组拥有者身份和实际频率。
- 开发版自动探针在建组前后各读取一次 Ark 屏幕参数，然后释放自己创建的连接和群组。
- “WiFi 直传测试文件”按钮和 `DirectTransferProbe` 会读取原有 WiFi 配置、建立临时群组、通过 Ark 专用的 `0x50/0x51/0x52` 命令发送和查询测试文件，最后比较配置并清理临时资源。密码只在内存中传给 Ark，不显示在日志。
- PASS 表示 Ark 报告 DONE，接收及总字节数等于打包 asset 的长度，原有 WiFi 配置查询未变，手机临时组清理完成。屏幕显示效果仍需目视确认。

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

M1 真文件传输探针（同样仅在 debug APK 中）：

```powershell
adb -s <手机序列号> shell am instrument -w org.framefilm.ark.dev/org.framefilm.ark.DirectTransferProbe
```

请先给应用授予附近蓝牙和附近 WiFi 权限，并确保手机没有其他 WiFi Direct 组。异常时探针请求取消，等待 Ark 报告恢复完成后再释放手机 HTTP 服务及直连组；最长可能等待数分钟。

## 后续阶段

1. 在已通过的单文件直传基础上，接入手机选图、预览、转换和真实进度展示。
2. 针对实际使用补齐取消、连接中断与重试体验，再逐步迁移网页中的文件管理和设备设置。
3. 基础传输稳定后完善通行证界面。当前仅有诊断界面，不是完整相册客户端，也不与小米互传私有协议互通。

第三方 ROM 声明支持 WiFi Direct 仅表示具备接口，兼容性以具体手机的实测结果为准。

## 当前验证记录（2026-10-07）

### 新手机：M1 真文件直传与屏幕显示通过

- Ark 使用 ESP-IDF 5.5.2 构建通过；应用大小 1,884,336 字节，应用分区剩余 61,264 字节。已向 COM15 的 Ark 写入应用区并完成 esptool 校验，保留 NVS 与 SD。烧录前应用备份为本地 `.output/ark/backups/pre-direct-m1-app.bin`。
- Android 调试 APK 构建、安装成功。修复了控制器声明的 Kotlin 递归类型推断错误。
- 首次联调发现 GO 已建立但 IP 地址尚未就绪：`owner=true, groupFormed=true, addressPresent=false, passphrasePresent=true`。客户端改为有限等待真实地址，并在失败时确认本次组是否释放。为调试失败恢复增加 `DirectTransferProbe -e cleanupGroup <精确组名>` 分支；只清理匹配且属于 GO 的组，不修改其他组。
- 实机还暴露出 BLE 通知早于 GATT 写完成的竞态。客户端现在同时等待写成功与设备回包，之后才开始下一条命令。
- 修复后同一 M1 探针返回 `result=PASS`、`cleanupCompleted=true`。Ark 经 WiFi 保存 43,232 字节已知有效的 720×480 单帧 `.film`，状态从 RESTORING 进入 DONE，接收字节数和总长均为 43,232；原有 WiFi 开关、SSID、密码和 API 地址查询结果前后一致。本机原配置为 WiFi 关闭、网络信息为空，尚未验证恢复已有路由器连接。
- 手机系统另确认回到 `InactiveState`，`groupFormed=false`，临时组已释放。测试文件使用独立时间戳名称，未覆盖用户原有图片。
- 本地证据：`.output/ark/logs/build-direct-m1-final.log`、`flash-direct-m1.log`、`android-direct-m1-gatt-fix-build.log`、`android-direct-m1-probe-gatt-fixed.log`。最终传输依据设备 BLE 状态回包；串口捕获在最终传输前已结束，不能声称最终串口日志验证了保存或屏幕刷新。
- 用户已目视确认“已显示新的测试图，正常”，文件传输、保存与实际屏幕显示闭环通过。当前验收限于本次单文件成功路径；取消、断电、满卡、长时间稳定性及吞吐量未做完整回归。

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
