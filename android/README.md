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

- 空工程和 M0 业务代码的 `:app:assembleDebug` 均构建通过；使用 JDK 21、Gradle 8.11.1、AGP 8.10.1、SDK 35。
- 小米 9（第三方 HyperOS 2 / Android 15、API 35）的 USB 调试已授权，ADB 连接正常。
- 流式和非流式 ADB 安装均停在系统安装会话已提交的阶段，没有返回完成或错误。用户确认 USB 安装开关已开启；两个挂起会话已清理，具体阻塞原因尚未确定。
- APK 已复制到手机 `Download/FrameFilm-Ark-M0-0.1.0-debug.apk`，等待通过手机系统安装器确认本地安装结果。
- BLE 实机查询、2.4GHz GO 建组及并行通信探针尚未运行，不能据构建成功判断手机直传兼容性。
