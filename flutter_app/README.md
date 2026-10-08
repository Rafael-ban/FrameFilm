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

本轮使用 Flutter stable 3.47.6 / Dart 3.13.5。Android compile/target SDK 35、minSDK 29、JVM 17；调试包名 `org.framefilm.ark.flutter.dev`，与现有客户端共存。当前签名仅用于开发。

本机大型依赖与构建目录已经迁到 D 盘，原 C 盘入口保留 Windows 目录联接（junction）。没有更改系统 PATH，也没有迁移整个 `.codex/worktrees`：其他工作树仍有正在运行的程序和未提交内容。

| 内容 | 本机实际位置 |
|---|---|
| Flutter SDK | `D:\dev-tool\FrameFilm-toolchains\flutter` |
| Android SDK / NDK | `D:\dev-tool\Android\Sdk` |
| Gradle 缓存与分发包 | `D:\dev-tool\gradle-home` |
| Dart Pub 缓存 | `D:\dev-tool\pub-cache` |
| 当前 Flutter 构建输出 | `D:\dev-tool\FrameFilm-build\flutter` |
| 本构建入口的临时文件 | `D:\dev-tool\FrameFilm-build\temp` |
| 当前 Ark 固件 build | `D:\dev-tool\FrameFilm-build\firmware-ark` |

从仓库根目录使用入口脚本。它为本次命令设置 SDK、缓存与 TEMP/TMP，命令结束后恢复进程环境；自动维护忽略的 `local.properties`。执行 `clean` 删除 build 联接后，下次使用入口会重建联接，防止输出重新落到 C 盘。

```powershell
$env:JAVA_HOME = 'C:/Program Files/Java/jdk-25' # 本机安装路径；其他电脑使用自己的兼容 JDK
./flutter_app/tool/flutter.ps1 pub get
./flutter_app/tool/flutter.ps1 analyze --no-pub
./flutter_app/tool/flutter.ps1 test --no-pub test/widget_test.dart
./flutter_app/tool/flutter.ps1 build web --no-pub
./flutter_app/tool/flutter.ps1 build apk --debug --target-platform android-arm64 --no-pub
```

这组 D 盘路径是当前电脑的开发配置，不是项目运行依赖。其他电脑可以按标准 Flutter 命令构建；也可以用 `FRAMEFILM_DEV_ROOT` 指定同结构存储目录。不同 checkout 应使用独立存储根目录，避免共享构建输出。已有非联接的 build 目录需要先迁移，脚本不会自动删除。Ark 的 build 联接如被 `fullclean` 删除，也需要重新创建或用 `idf.py -B <D盘目录>` 指定输出位置。

浏览器预览从 `build/web` 启动静态服务，本轮地址 `http://127.0.0.1:8770/`。它用于查看同一份 Flutter 界面，无 BLE 能力；手机端桥接仍需 Android 构建及实机验证。预览不是把网页嵌进 App。

## 验证记录（2026-10-08）

- Flutter analyze：通过，无问题。
- 3 项 widget 测试：通过。覆盖六页导航、离线边界、主题选择、表单切页保留、先连接后补名称、过场结束/断连取消及明确 ForFilm 偏好。测试使用 FakeGateway，不是真蓝牙验收。
- Web 构建通过，电脑实际打开并检查两套主题。
- 首次 Android 构建因 C 盘空间不足失败。迁移依赖、构建输出和临时目录后重试成功，NDK 28.2.13676358 安装于 D 盘。
- Android arm64 debug APK 已生成：`build/app/outputs/flutter-apk/app-debug.apk`。此结果确认 Kotlin 桥接和原生依赖编译通过；不能替代蓝牙实机验收。
- 首次实机出现白屏：旧 APK 缺少 `kernel_blob.bin`，Dart VM 没有应用 isolate。清除迁移残留的 `.dart_tool/flutter_build` 并执行 `pub get` 后重新打包，手机已正常显示页面。C/D 盘别名混用的旧输出记录与 Flutter 增量清理机制是本次构建缺文件的相关证据，不是页面或蓝牙逻辑故障。
- 构建入口新增标准 debug APK 检查：kernel、isolate snapshot 必须非空，VM snapshot 必须存在（本 SDK 的 VM 占位文件为 0 字节，允许为空）。本次修复 APK 为 81,936,838 字节。
- 实机 `25042PN24C`（系统报告 Android 17）已更新安装并完成启动、扫描、连接、首次读取和手动刷新验证：设备名 `FRAMEFILMARK-伊卡洛斯sama`，电量 100%，屏幕 720×480。连接后界面切为 PRTS 主题。
- 本轮仅查询屏参、电量与设备名，没有修改 Ark 参数或文件。图片传输、取消重试、后台保持与长时间稳定性不在本轮验收范围内。

## 下一步

1. 已完成 Android APK 构建与扫描/连接/只读信息实机验证；后续先补功能，最终手机 UI 以现有网页视觉与交互为准。
2. 接入现成 film 导入、WiFi 直传、进度、取消、清理和重试；现有内核取消重试的历史实机缺口仍未关闭。
3. 完成通行证头像、代号、编号、所属、签名与 SD 读写，增加草稿持久化。
4. 迁移图片转换与高级动画功能；OTA 的固件擦除超时独立处理。
