# Ark 保存显示修复与统一传输推进

用户授权顺序：先修复上传后旧图多刷，再整理 BLE 控制 + WiFi 数据传输，接入 App 固件 OTA。功能优先，暂不重做 UI。

## 当前基线
- main / 工作树起点：c5a8de2；设备运行 3.2.5，镜像 c3d4ddda362449dccc7a70fc0c031339abf4b7e1aa47e0b2ab0cf6ea17edb102。
- film WiFi 使用 PSRAM 接收缓冲，SDMMC 通过内部 sector DMA 缓冲写卡；实机保存和竖屏显示已通过。
- 已复现：SYS_EVT_FILE_LIST 先排队旧 file_id=3，SYS_EVT_FILE_SAVED 再排队新 file_id=5，旧图彩刷额外约 29 秒。
- 构建和大型依赖保持 D 盘；串口只被动监听 COM15，DTR/RTS=false；不使用 esptool 复位握手。
- codex/ark-flutter 已删除；自然里程碑提交并快进到 main，不重建该分支。

## 阶段 A：仅显示最新保存图片
- [x] 修复事件时序，保留首次加载、静默保存和正常目录刷新语义。
- [x] 一轮直接相关验证与 Ark 增量编译。
- [x] 部署后复测一次新图 WiFi 保存，日志只排队新图，核对实体显示。
- [x] 记录证据，随本次修复提交。

实机结果：3.2.5 修复版 OTA 成功，档案保留；新图提交后仅排队 id=5，提交到显示请求 70ms，未重刷旧 id=3（该旧图彩刷耗时 28.95s）。手机 WiFi 保存 43232/43232、error=0、清理完成，用户确认显示正常且方向正确。证据：`ark-display-order-fix-20261008.json`。

## 阶段 B：共用传输流程与 App OTA
- [x] 确定固件/客户端边界与协议：会话共用，film 与 OTA 分别提交；保留 BLE OTA 备用。
- [x] 整理连接、实际进度、取消、清理、重试；加入 OTA 分支的大小/目标/完整性检查。
- [x] App 导入固件、显示升级阶段、重启后确认构建指纹；保持 Android 9 最低支持。
- [ ] 针对受影响路径做最小验证，编译产物与实机结果分开记录。
- [x] 更新协议/使用文档，记录已通过与待实测项；本地提交并合入 main，不推送。

约束：100% 仅表示传输完成；只有文件提交或 OTA 重连确认才成功。当前自动启动回滚未启用，不能声称已支持。正式实现之前核对其范围及启动确认策略，不仅修改配置开关。取消后第一版可从头重试，无需推测性加入断点续传。
### 已实现的接口合同（手机 WiFi OTA 实机验收进行中）

以下合同已在固件和 App 实现。当前 Ark 实现与协议文档未发现 0x56~0x58 已分配；保留 film 的 0x50~0x52 和旧 BLE OTA 0x10~0x13，新增仅用于 Ark。

| 新命令 | 合同 |
|---|---|
| 0x56 BUILD_INFO_GET | 返回能力版本、项目身份、运行镜像 version 与实际 app ELF SHA256；升级不能仅靠版本字符串确认。 |
| 0x57 DIRECT_OTA_START | size(u32 BE) + 文件SHA256(32B) + SSID\0PASSWORD\0URL\0，总 DATA ≤192B；只接受 Ark app .bin，沿用 BLE 控制、手机 HTTP 服务、设备 WiFi 拉流。 |
| 0x58 DIRECT_OTA_APPLY | 仅在镜像校验完成、原 WiFi 已恢复的 READY 阶段提交启动分区，回包后安排重启；提交点之后不承诺取消。 |

- **阶段语义**：共用 0x51 的 11B 状态格式和 0x52 取消，保留 film 状态 0~6，OTA 新增 READY=7、APPLYING=8。流式写 inactive Flash，检查目标、容量、长度和 SHA256，通过 esp_ota_end 后才 READY，apply 才切 boot partition。取消/失败 abort，旧启动分区不变，清理后从头重试。App 重连同一设备并核对实际 ELF SHA 才成功，超时显示结果待确认；本阶段不启用或宣称自动回滚。
- **固件边界**：service_wifi.{h,c} 共用连接/HTTP/取消/恢复，仅按 kind 分流 film 保存与 OTA 写入；保持 film PSRAM 缓冲、保存所有权、提交事件及原状态语义。service_ota.{h,c} 增加可报错、abort、校验结束与延后 activate 内部接口；service_ble.{h,c} 增加命令、互斥并同步协议文档。WiFi 下载结束不可直接调用当前会重启的 service_ota_stop。
- **客户端边界**：共享 DirectTransferCoordinator/ArkSession 增加 kind、固件元数据和 rebooting/confirming，复用 P2P、清理、重试；HTTP server 支持 .bin 路径，保留 film 命名。Flutter ArkBridge/device_gateway.dart/app.dart 增加固件导入与阶段展示，固件不走 film 转换校验。保持 minSdk 28 和现有 Android 9 P2P/权限分支。
- **最小验证**：少量直接协议/镜像解析及错误路径检查，Ark 增量构建和受影响 Android/Flutter 编译各一轮；实机验证升级后 ELF SHA 匹配、取消后旧版本可用且能重试、错误镜像不切启动分区。共享 HTTP 路径改动后补一次 film WiFi 保存冒烟，核对最新图显示与原 WiFi 恢复；分别记录构建与实机证据。
2026-10-08：用户明确不推送，后续只本地提交/合入 main。3.2.5 是彩蛋，不作为版本比较或更新判断；身份使用实际 ELF SHA256，文件 SHA256 用于完整性。静态/定向测试和两端编译通过；首次 BLE 部署支持 WiFi OTA 的构建已成功，ELF 2739efdaaa94435eac9faf2242d87ae58426b26b0c988d44ef3e84171ed800e5。固件已放 Download/ark_wifi_ota.bin，后续实机结果见下节。

### 2026-10-08 手机 WiFi OTA 与后续方向

- 手机 App 已实际完成 WiFi OTA：1,854,672/1,854,672 字节，重连后显示实际 ELF SHA256 匹配；被动串口记录重启及新 ELF 前缀 df9b5eabd。升级前后显示版本均为 3.2.5。
- 最终镜像文件 SHA256：df51ff0065f9b1dd8148722390ac4ec000782e3379ab96fa4de625257a215824；ELF SHA256：df9b5eabd7cd44ec957af7549f220c69501a3f888cb681ec64bb7810f05a1673。
- 新 OTA 中途取消/重试、同构建跳过的手机实测及本轮共享流程改动后的 film 冒烟尚未完成，不将 host/Widget 测试当作实机验收。旧图重复刷新修复及此前 film 保存/显示验收继续有效。
- 被动串口采集已自然结束；手机临时插电常亮设置已恢复原值 0。
- 用户要求升级页不显示“版本彩蛋”，界面改为“固件版本”；此文案修改尚未重新安装到手机。
- 下一阶段先统一 Flutter 功能：补明确标注演示数据的 Web 设备模式，电脑预览同份 UI 与状态流程，再编译 Android 实测 BLE/WiFi/OTA。当前 Web 的 PlatformDeviceGateway 不支持设备操作，不能把静态预览视为完整功能验证。
- GitHub 在线获取列入下一阶段：当前 CI 只上传 Actions artifact，尚无 Release 发布。计划 Release 提供原始 Ark app.bin 及构建摘要，下载后复用现有解析、完整性校验和 OTA；不以 3.2.5 或版本大小作为更新依据。
- 功能缺项顺序：设备参数/时间/遥控、通行证实际读写、图片转换、动画编辑，最后统一两套主题细节。维持 Android 9 下限、构建在 D 盘、暂不推送。

### 阶段 C1：Flutter 电脑交互预览

- 已接入仅内存的 PreviewDeviceGateway，Web query `preview=1` 或显式按钮进入；Android默认保持平台桥接。所有页面持续显示模拟标识，不访问真实设备或文件。
- 同份页面支持模拟连接、film/固件元信息、传输进度、取消/重试、OTA重启确认，以及失败恢复、待确认、相同构建跳过场景；切换场景/重置/退出清理计时器。
- 4项新测试、受影响文件analyze和Web构建通过。Navigator内演示栏修复后仅复测下拉交互，Edge浏览器确认film取消重试完成及窄屏场景选择。
- 本机服务：http://127.0.0.1:8770/?preview=1，输出仍在 D:/dev-tool/FrameFilm-build/flutter/web。
- 下一小步：GitHub Release原始应用固件获取与下载，复用现有校验/OTA；现阶段未发布远端或更改手机。

### 阶段 C2：GitHub 在线固件

- 设置页已接入固定公开仓库 Releases 查询（最近20个、正式发布、精确 frame_film_ark.bin），按发布时间选择候选。版本字符串不参与更新判断。
- Android支持下载字节进度、取消、重试下载及校验成功后导入；4MiB/长度/可选GitHub资产SHA检查后，复用ArkFirmware.inspect验证目标与镜像。失败或取消保留原文件；下载不自动启动OTA。
- 独立firmwareDownload快照与取消入口，避免与设备WiFi OTA的transfer状态混淆。Web支持真实查询，模拟入口可演示下载，不访问真实设备。
- CI新增默认关闭的main手动publish_release选项，发布仅该次构建的原始app.bin，tag使用运行号/尝试号。仅改本地配置，未执行发布或远端CI。
- 验证：Flutter定向7项测试/analyze、Android下载helper host probe、APK与Web构建通过；发布job YAML和bash语法通过。Edge浏览器真实API HTTP200空列表，模拟下载取消后重新下载完成，状态仍为导入待升级。
- 本轮APK为 D:/dev-tool/FrameFilm-build/flutter/app/outputs/flutter-apk/app-debug.apk（97,311,403字节），没有安装手机。原固件和设备数据未更改。
- 待实测：正式Release附件下载/网络重定向、Android取消/生命周期及下载后实机OTA。当前公开Releases为空，无线上新固件可验收；保持不推送、不发布。
