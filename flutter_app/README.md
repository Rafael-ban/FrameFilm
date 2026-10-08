# FrameFilm Ark · Flutter

Flutter 原生界面、Android BLE 桥接与 film 文件 Wi-Fi Direct 传输。使用 Flutter widgets，不使用 WebView；先完成功能，最终手机 UI 再与网页统一。

## 当前范围

- 六页：连接、Frame、Film、动画、通行证、设置。
- 两套视觉：原 ForFilm 暖色圆角、明日方舟 PRTS 炭灰黑白冷蓝。
- 默认自动模式：离线显示 ForFilm，Ark GATT 就绪后切主题并播放一次过场。名称后续回包不会重播；断连立即取消。明确选择 ForFilm 时保持该外观。
- 设置可离线预览明日方舟；主题偏好目前只在当前应用会话内保留。
- Android bridge 接入扫描、权限、连接、断开、0x42 屏参、0x23 电量与 0x54 名字读取。命令同时等待写入完成和同通道回包；超时断开以丢弃旧连接的迟到回包。
- 通行证提供代号、编号、职能的内存表单预览；不是最终设备档案布局，也不写 SD，退出应用后不保留。
- Film 页支持系统文件选择器导入已有 `.film`、格式与 720×480 尺寸校验、Wi-Fi Direct 直传、设备实际字节进度、取消、清理状态与重试。单文件上限 32 MiB；非法文件或取消选择保留上次导入。合法 ASCII 文件名最长 50 字符（包含 `.film`），其他名称映射为本次导入的稳定短名，以预留固件保存后缀。
- 传输通过 BLE 协商手机创建的临时 2.4GHz 直连组，不需要路由器或用户手动开热点。重试从头传输，不是断点续传；只有设备确认保存且连接清理完成才显示成功。
- 导入副本保存在应用缓存；替换、清除与会话结束时释放，进程启动清理上次遗留副本。活动传输和未完成的清理期间不允许替换文件。
- Frame 与动画仍未接入图片转换或编辑操作。

## 结构与复用

- `lib/app.dart`：原生页面、主题与连接过场。
- `lib/device_gateway.dart`：平台通道、设备快照与页面控制器。
- `android/app/src/main/kotlin/org/framefilm/framefilm_ark/ArkBridge.kt`：唯一 ArkSession、权限、通知读取与系统文件导入。
- Android Gradle 从仓库现有 `../android/app/src/main/java` 引入通信源码，排除旧 Activity/UI/相机 Provider；没有复制第二套 BLE/P2P 状态机。
- 桥接在 Flutter 宿主生命周期内持有会话，页面切换不关闭连接；关闭 engine 时释放。暂不承诺后台常驻或进程终止后自动恢复；进程结束后需重新导入文件。
- `assets/rhodes_logo.png` 来自仓库 `tools/ui-assets/boot_logo_mono.png`，源图说明见 `tools/ui-assets/boot-logo.md`。

## Android 9 兼容性

最低 Android 9（API 28）。Android 9 使用公开旧版 `createGroup(channel, listener)`，读取系统生成的名称和密码；系统不提供强制指定或查询组频段的公开 API，若当前 5GHz Wi-Fi 使 Ark 无法连接，请先断开该 Wi-Fi 后重试。Android 10 及以上使用可明确指定 2.4GHz 的接口。蓝牙扫描和旧版直连需要定位授权，部分系统还需要开启定位服务。

直传准备阶段不再发送切换图片 app 的命令，避免先重放旧图。固件保存时按帧数归入图片/动画目录；已处在相应内容页时由固件已有保存事件刷新，其他页面保持原画面。Android 9 尚需对应系统的实机验证，当前连接手机报告 Android 17。

参考：[Android Wi-Fi Direct API](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pManager)、[API 29 起支持指定频段](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pConfig.Builder)。

## 工具链与命令

本轮使用 Flutter stable 3.47.6 / Dart 3.13.5。Android compile/target SDK 35、minSDK 28（Android 9）、JVM 17；调试包名 `org.framefilm.ark.flutter.dev`，与现有客户端共存。当前签名仅用于开发。

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

浏览器预览从 `build/web` 启动静态服务，本轮地址 `http://127.0.0.1:8770/`。它用于查看同一份 Flutter 界面，无 Android BLE、文件导入或 Wi-Fi Direct 能力；手机端桥接仍需 Android 构建及实机验证。预览不是把网页嵌进 App。

## 验证记录（2026-10-08）

- Flutter analyze：通过，无问题。
- 第一阶段 3 项 widget 测试：通过。覆盖六页导航、离线边界、主题选择、表单切页保留、先连接后补名称、过场结束/断连取消及明确 ForFilm 偏好。测试使用 FakeGateway，不是真蓝牙验收。
- Web 构建通过，电脑实际打开并检查两套主题。
- 首次 Android 构建因 C 盘空间不足失败。迁移依赖、构建输出和临时目录后重试成功，NDK 28.2.13676358 安装于 D 盘。
- Android arm64 debug APK 已生成：`build/app/outputs/flutter-apk/app-debug.apk`。此结果确认 Kotlin 桥接和原生依赖编译通过；不能替代蓝牙实机验收。
- 首次实机出现白屏：旧 APK 缺少 `kernel_blob.bin`，Dart VM 没有应用 isolate。清除迁移残留的 `.dart_tool/flutter_build` 并执行 `pub get` 后重新打包，手机已正常显示页面。C/D 盘别名混用的旧输出记录与 Flutter 增量清理机制是本次构建缺文件的相关证据，不是页面或蓝牙逻辑故障。
- 构建入口新增标准 debug APK 检查：kernel、isolate snapshot 必须非空，VM snapshot 必须存在（本 SDK 的 VM 占位文件为 0 字节，允许为空）。本次修复 APK 为 81,936,838 字节。
- 实机 `25042PN24C`（系统报告 Android 17）已更新安装并完成启动、扫描、连接、首次读取和手动刷新验证：设备名 `FRAMEFILMARK-伊卡洛斯sama`，电量 100%，屏幕 720×480。连接后界面切为 PRTS 主题。
- 第一阶段只读测试仅查询屏参、电量与设备名；第二阶段传输测试记录见下。

## 第二阶段：film 直传与 CI

- `flutter analyze --no-pub`：通过。6 项 widget 测试已通过（其中一项滚动定位失败修正后，仅复测该项）。新增覆盖导入、实际进度、取消、清理与重试状态；FakeGateway 不替代真实设备。
- Android arm64 debug APK 编译、运行资源检查与覆盖安装通过；最终包 81,982,381 字节。`aapt dump badging` 确认 `sdkVersion: 28`，支持安装到 Android 9。
- 实机已成功导入 43,232 字节、720×480 的测试 film；初次直传到达设备保存阶段后返回错误 3。定位到旧 HTTP 服务附加时间戳后，加上固件 `.ffupload.part` 后缀超过 FATFS 64 字符限制；已将实际发送名限制为 50 字符以内并取消每次重试的时间戳前缀，UI 与设备保存名一致。
- 修复后真机：导入通过；取消显示“已取消”，临时服务与连接清理确认；取消后重试可启动并接收完整 43,232 字节，仍在设备提交阶段返回状态 5 / 错误 3。100% 没有误报成功，失败后清理确认。所选 `ark_flutter_test.film` 与仓库样例 SHA-256 完全相同，用户确认 SD 卡可打开已有图片。设备最终保存、显示尚未验收通过，需要后续读取固件提交日志定位；未格式化 SD、未修改设备固件。
- Android 9 分支本轮完成源码与 APK 编译检查，尚无 Android 9 真机结果；不能用 Android 17 手机代替该项验收。
- 自动构建配置及产物说明见 [Ark 自动构建](../docs/development/ci-builds.md)。分别构建 Android debug APK 和 ESP-IDF 5.5.2 Ark 固件，保留构建日志；首次远端 [Ark builds](https://github.com/Rafael-ban/FrameFilm/actions/runs/37736539292) 的两项编译及产物上传已通过。

## 保存故障定位补充（2026-10-08）

- 本轮接入过 Android 9 `TYH201H` 和 `25042PN24C` 两台手机，测试结果分开记录。Android 9 扫描无结果的原因尚未确定，不能判定为权限或机型不兼容。
- `25042PN24C` 已覆盖安装当前本地 APK，手机安装包与本地 SHA-256 均为 `174326575dbc265ebe73bbadc9ecd8db0f0fc336aad5e06424134fbd95c570d6`；扫描、连接成功。
- 当前 APK 导入缓存与仓库测试 film SHA-256 相同，发送名为 `ark_flutter_test.film`。WiFi 实测 UART 仍返回 `state=5 bytes=43232/43232 error=3`，不是仅凭手机 100% 进度判断。
- 同一份 film、同一文件名通过 BLE 静默保存成功，UART 确认 `Film transfer saved: 43232 bytes`。此结果支持继续调查 WiFi 保存路径，不能据此宣称 WiFi 保存已修复。
- 保存诊断固件已增量编译并通过 BLE OTA 部署，大小 1,852,064 字节，镜像 SHA-256 `c4b0c586d0a2cb18330817ef50200f9f63729d7bda80faf0729f474ac66e5abf`，启动 ELF 摘要前缀 `da7026223`；设备恢复蓝牙，通行证档案回读与升级前一致。
- 诊断实测明确失败于文件头校验：`Invalid film color count: 0 (format=0)`，路径 `/sdcard/film/ark_flutter_test.film.ffupload.part`。导入源应为 format=1，故目前证据是 WiFi 路径最终读回头部与源不符；尚未确定差异出现在 HTTP 接收还是存储写入/读回，不能宣称根因或修复完成。
- 下一步只需对照手机实际 HTTP 响应、设备接收缓冲的头部和 SD 临时文件头，定位首次差异；不要通过放宽 film 校验来让损坏文件通过。当前无扫描逻辑修改。

## 下一步

1. 继续完成功能后，再按现有网页统一手机 UI。
2. 完成通行证头像、代号、编号、所属、签名与 SD 读写，增加草稿持久化。
3. 迁移图片转换与高级动画功能；后台保持、进程恢复与长时间稳定性另行验证。
