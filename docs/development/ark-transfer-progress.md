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
- [ ] 确定固件/客户端边界与协议：会话共用，film 与 OTA 分别提交；保留 BLE OTA 备用。
- [ ] 整理连接、实际进度、取消、清理、重试；加入 OTA 分支的大小/目标/完整性检查。
- [ ] App 导入固件、显示升级阶段、重启后确认版本；保持 Android 9 最低支持。
- [ ] 针对受影响路径做最小验证，编译产物与实机结果分开记录。
- [ ] 更新协议/使用文档，提交 main。

约束：100% 仅表示传输完成；只有文件提交或 OTA 重连确认才成功。当前自动启动回滚未启用，不能声称已支持。正式实现之前核对其范围及启动确认策略，不仅修改配置开关。取消后第一版可从头重试，无需推测性加入断点续传。
### 下一阶段待实现设计（尚未落代码、未部署）

以下为实施合同草案，命令和新增状态均未实现。当前 Ark 实现与协议文档未发现 0x56~0x58 已分配；保留 film 的 0x50~0x52 和旧 BLE OTA 0x10~0x13，新增仅用于 Ark。

| 新命令 | 拟定合同 |
|---|---|
| 0x56 BUILD_INFO_GET | 返回能力版本、项目身份、运行镜像 version 与实际 app ELF SHA256；升级不能仅靠版本字符串确认。 |
| 0x57 DIRECT_OTA_START | size(u32 BE) + 文件SHA256(32B) + SSID\0PASSWORD\0URL\0，总 DATA ≤192B；只接受 Ark app .bin，沿用 BLE 控制、手机 HTTP 服务、设备 WiFi 拉流。 |
| 0x58 DIRECT_OTA_APPLY | 仅在镜像校验完成、原 WiFi 已恢复的 READY 阶段提交启动分区，回包后安排重启；提交点之后不承诺取消。 |

- **阶段语义**：共用 0x51 的 11B 状态格式和 0x52 取消，保留 film 状态 0~6，OTA 拟新增 READY=7。流式写 inactive Flash，检查目标、容量、长度和 SHA256，通过 esp_ota_end 后才 READY，apply 才切 boot partition。取消/失败 abort，旧启动分区不变，清理后从头重试。App 重连同一设备并核对实际 ELF SHA 才成功，超时显示结果待确认；本阶段不启用或宣称自动回滚。
- **固件边界**：service_wifi.{h,c} 共用连接/HTTP/取消/恢复，仅按 kind 分流 film 保存与 OTA 写入；保持 film PSRAM 缓冲、保存所有权、提交事件及原状态语义。service_ota.{h,c} 增加可报错、abort、校验结束与延后 activate 内部接口；service_ble.{h,c} 增加命令、互斥并同步协议文档。WiFi 下载结束不可直接调用当前会重启的 service_ota_stop。
- **客户端边界**：共享 DirectTransferCoordinator/ArkSession 增加 kind、固件元数据和 rebooting/confirming，复用 P2P、清理、重试；HTTP server 支持 .bin 路径，保留 film 命名。Flutter ArkBridge/device_gateway.dart/app.dart 增加固件导入与阶段展示，固件不走 film 转换校验。保持 minSdk 28 和现有 Android 9 P2P/权限分支。
- **最小验证**：少量直接协议/镜像解析及错误路径检查，Ark 增量构建和受影响 Android/Flutter 编译各一轮；实机验证升级后 ELF SHA 匹配、取消后旧版本可用且能重试、错误镜像不切启动分区。共享 HTTP 路径改动后补一次 film WiFi 保存冒烟，核对最新图显示与原 WiFi 恢复；分别记录构建与实机证据。