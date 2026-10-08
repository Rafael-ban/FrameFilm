# FrameFilm Ark · Flutter

第一阶段原生界面与 Android BLE 桥接。使用 Flutter widgets，不使用 WebView。

## 当前范围

- 六页：连接、Frame、Film、动画、通行证、设置。
- 两套视觉：原 ForFilm 暖色圆角、明日方舟 PRTS 炭灰黑白冷蓝。
- 默认自动模式：离线显示 ForFilm，Ark GATT 就绪后切主题并播放一次过场。名称后续回包不会重播；断连立即取消。明确选择 ForFilm 时保持该外观。
- 设置可离线预览明日方舟；主题偏好目前只在当前应用会话内保留。
- Android bridge 接入扫描、权限、连接、断开、0x42 屏参、0x23 电量与 0x54 名字读取。命令同时等待写入完成和同通道回包；超时断开以丢弃旧连接的迟到回包。
- 通行证提供代号、编号、职能的内存表单预览；不是最终设备档案布局，也不写 SD，退出应用后不保留。
- Frame、Film、动画目前展示阶段范围，尚未接入图片转换、传输或编辑操作。

## 结构与复用

- `lib/app.dart`：原生页面、主题与连接过场。
- `lib/device_gateway.dart`：平台通道、设备快照与页面控制器。
- `android/app/src/main/kotlin/org/framefilm/framefilm_ark/ArkBridge.kt`：唯一 ArkSession、权限与通知读取。
- Android Gradle 从仓库现有 `../android/app/src/main/java` 引入通信源码，排除旧 Activity/UI/相机 Provider；没有复制第二套 BLE/P2P 状态机。
- 桥接在 Flutter 宿主生命周期内持有会话，页面切换不关闭连接；关闭 engine 时释放。首阶段不承诺后台常驻或进程终止后恢复。
- `assets/rhodes_logo.png` 来自仓库 `tools/ui-assets/boot_logo_mono.png`，源图说明见 `tools/ui-assets/boot-logo.md`。

## 工具链与命令

本轮使用 Flutter stable 3.47.6 / Dart 3.13.5。SDK 安装于仓库忽略目录 `.output/toolchains/flutter`，未修改系统 PATH。Android compile/target SDK 35、minSDK 29、JVM 17；调试包名 `org.framefilm.ark.flutter.dev`，与现有客户端共存。当前签名仅用于开发。

从 `flutter_app` 执行（或将 Flutter 加入 PATH 后使用普通命令）：

```powershell
& ../.output/toolchains/flutter/bin/flutter.bat pub get
& ../.output/toolchains/flutter/bin/flutter.bat analyze --no-pub
& ../.output/toolchains/flutter/bin/flutter.bat test --no-pub test/widget_test.dart
& ../.output/toolchains/flutter/bin/flutter.bat build web --no-pub
& ../.output/toolchains/flutter/bin/flutter.bat build apk --debug --target-platform android-arm64 --no-pub
```

浏览器预览从 `build/web` 启动静态服务，本轮地址 `http://127.0.0.1:8770/`。它用于查看同一份 Flutter 界面，无 BLE 能力；手机端桥接仍需 Android 构建及实机验证。预览不是把网页嵌进 App。

## 本轮验证与阻塞（2026-10-08）

- Flutter analyze：通过，无问题。
- 3 项 widget 测试：通过。覆盖六页导航、离线边界、主题选择、表单切页保留、先连接后补名称、过场结束/断连取消及明确 ForFilm 偏好。测试使用 FakeGateway，不是真蓝牙验收。
- Web 构建通过，电脑实际打开并检查两套主题。
- Android APK 构建失败：C 盘空间不足，失败点为资源编译和原生库合并，未产出 APK，不能声称 Android 编译或 BLE 实机通过。
- 已清理本次失败的 `build/app` 中间产物，并撤回本次新装的 NDK 28.2.13676358，恢复约 2.3 GiB 空间。D 盘检查时约 88 GiB 空闲；下轮先把大型 Android/Gradle 构建目录安排到 D 盘，再恢复 APK 构建。
- 没有安装手机、配对设备或向 Ark 写入数据。

## 下一步

1. 完成 Android APK 构建与扫描/连接/只读信息实机验证。
2. 接入现成 film 导入、WiFi 直传、进度、取消、清理和重试；现有内核取消重试的历史实机缺口仍未关闭。
3. 完成通行证头像、代号、编号、所属、签名与 SD 读写，增加草稿持久化。
4. 迁移图片转换与高级动画功能；OTA 的固件擦除超时独立处理。
