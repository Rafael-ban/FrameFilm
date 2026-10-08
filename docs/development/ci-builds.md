# Ark 自动构建

工作流：`.github/workflows/ark-builds.yml`。仅构建 Flutter Android app 与 `firmware/frame_film_ark`，不构建经典固件或 Dock。

## 触发与下载

- 推送到 `main` 或 `codex/ark-flutter`、向 `main` 提交 PR 时，限定 Flutter、复用的 Android Kotlin 源码、Ark 固件及工作流自身路径。
- 当前开发分支推送即可自动运行；工作流进入默认分支后，可从 GitHub → Actions → Ark builds → Run workflow 手动运行。
- 两个 job 独立运行，完成后从该次运行的 Artifacts 下载。构建失败时保留已生成的日志；SDK 安装或 checkout 阶段失败请查看 Actions 步骤日志。

## Android

固定 Flutter 3.47.6（随附 Dart 3.13.5）、Temurin JDK 21；Gradle 9.3.1、AGP 9.1.0、Kotlin 2.4.0 与 Android SDK 配置由仓库文件控制。JDK 21 用于运行构建，应用仍保持 Java/Kotlin 17 编译目标，最低安装系统 Android 9（API 28）。CI APK 包含 Flutter 默认支持的多个 Android ABI，本机快速验证包为 arm64。

仓库忽略 Gradle wrapper 脚本和 jar。CI 使用同版本 Flutter 在临时目录生成 Android 项目，仅复制 wrapper 脚本/jar，不覆盖仓库中的 Gradle 配置和业务代码。

产物 `ark-flutter-debug-apk` 包含可安装的 `app-debug.apk`，使用临时 debug 签名，仅供开发构建；签名可能与本地或其他 CI 运行不同，不保证覆盖安装兼容，正式可更新包需使用稳定签名。上传前检查 APK 中 `kernel_blob.bin`、`isolate_snapshot_data` 存在且非空，`vm_snapshot_data` 必须存在但允许为零字节。日志位于 `ark-flutter-build-logs`。

## Ark 固件

使用 Espressif 官方 `espressif/idf:v5.5.2` 容器与仓库现有 `sdkconfig`（ESP32-S3、自定义 OTA 分区），不执行 `set-target` 或重置配置。依赖按 `dependencies.lock` 从 Espressif registry 解析；不依赖本机 build/managed_components。

`ark-esp32s3-firmware` 包含：

- `frame_film_ark.bin`：应用镜像，用于现有 OTA 流程。
- `frame_film_ark-flash.bin`：按构建生成的 flash_args 合并的串口烧录镜像，从地址 `0x0` 写入；此操作会覆盖镜像范围内的现有数据，应先备份设备数据。
- bootloader、partition-table、OTA 初始化镜像，以及 flash_args/flasher_args.json：按该次构建生成的偏移进行分文件烧录。
- ELF、map、sdkconfig：用于定位问题和核对构建配置。

`ark-firmware-build-logs` 包含构建、merge-bin、`idf.py size` 报告及生成的 IDF 日志。CI 不连接设备、不刷机、不发布 GitHub Release。

## 验证范围

本地只检查工作流结构与相关配置；远端 Actions 尚未运行，首次运行才验证 GitHub 下载、容器、依赖解析及完整编译。编译成功和 APK 资源检查通过不代表蓝牙、相机、屏幕刷新或设备 OTA 已经实测。

参考：[Flutter action](https://github.com/subosito/flutter-action)、[Gradle Java compatibility](https://docs.gradle.org/current/userguide/compatibility.html)、[Espressif 官方 CI 容器](https://github.com/espressif/esp-idf-ci-action)、[IDF 5.5.2 merge-bin 实现](https://github.com/espressif/esp-idf/blob/v5.5.2/tools/idf_py_actions/serial_ext.py)。

