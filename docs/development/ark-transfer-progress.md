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

### 阶段 C3：设备设置、状态隔离和 Release 构建（2026-10-09）

- 自动休眠、定时唤醒、唤醒间隔、名称后缀、时间/时区已接入 Flutter/Android，并提供同接口 Web 模拟操作。SET 无 ACK 的字段保存后 GET 回读；现有协议不变，固件未改。
- 修复 Film 页面串入固件升级状态。按传输 kind 投影各页面，保留设备共享互斥、取消与重试约束。
- 用户最终决定：Web 为 debug 预览，APK 为 Release 编译，暂用 debug 签名，后续再配置发行密钥。没有生成密钥。默认构建模式和 CI 产物名已同步。
- 设备设置 5 项和页面隔离 9 项测试通过，定向 analyze 通过；Edge 模拟保存间隔/中文名称/时间、OTA 期间 Film 显示通过。
- Web debug 与 Android arm64 Release 构建通过；APK 17,967,222 字节，AOT/引擎检查通过。第一次 Maven TLS 下载失败，Flutter 自带重试恢复，未禁用检查。
- 预览服务 8770；未安装手机、未修改真实设备，不把模拟验证算作 BLE 持久化或重启广播验收。继续本地提交/合入 main，不推送。
- 后续缺项：设备遥控、通行证实际读写与草稿、Frame 图片转换、动画编辑；功能补齐后统一 UI。
### 阶段 C4：通行证与遥控（2026-10-09）

- Flutter通行证接入原网页v1资料格式和440×608 FFUI渲染；头像中心裁切128×160、阈值160黑白预览与发送位图一致。完整字段、布局限制、未设置标记、头像移除均可操作。
- 本地草稿使用Web localStorage / Android SharedPreferences；切页保留，异步读取校验不覆盖更新的编辑。读取前确认替换，失败/取消保留草稿。
- 原生0x53完整分块读取；BLE先发送profile.bin再profile.json，最后回读JSON核对。保存非双文件原子事务、无位图ACK，明确部分完成和实体显示待确认。取消断连隔离旧回包，重新连接后从头发送，与现有设备/传输任务互斥。
- 遥控0x4E核对echo（只表示收到），上下遵循ForFilm映射；快捷页面0x4B后查询0x4C确认。固件和协议未改变。
- 定向测试共9项通过：schema/布局/位图4、编辑草稿和离页读取2、任务互斥及取消重试2、现有六页导航表单1。analyze无问题，Web debug / Android arm64 Release构建通过；APK18,180,426字节，临时debug签名、Android9下限保留。
- 未安装手机或写入真实Ark，SD双文件保存、回读以及实体画面仍需真机验收。后续主要缺项为图片转换、动画编辑、最终UI统一。
- Edge浏览器已完成头像选择、四字段编辑、模拟发送、确认覆盖后回读、遥控点击及跨页草稿保留。最初探针对未滚入视口的按钮和Canvas语义文本定位不当，改用滚动与可访问性group后通过；浏览器未出现脚本异常。

### 阶段 C5：Frame 图片生成 film（2026-10-09）

- 默认逻辑竖屏480×720，复用此前实机方向映射；图片选择、铺满/完整显示、90度旋转、缩放与平移、亮度/对比/饱和、可选Floyd–Steinberg抖动已接入。
- 同份Dart代码输出六色v1（172832B）或黑白MonoFast（43232B）；最终PNG和film来自相同量化像素，物理头部720×480不变。Web可下载真实film，再转入Film页复用模拟传输；Android接收生成字节后校验缓存并沿用WiFi链路。
- 取消/加载失败保留旧内容，参数变化禁用过期结果，切页中止未完成任务，清除释放本次图片。导入Film与既有下载/OTA/通行证互斥，失败保留原导入。
- 定向测试7项通过（codec3、编辑2、gateway2），受影响代码analyze与Web debug构建通过。原生导入桥仅源码检查；按用户新要求，本轮不构建/安装APK，后续功能集中完成后再统一编译和实机测试。
- 继续功能顺序：动画帧序列/播放参数，再扩展图片高级算法，最后统一UI。保持本地提交/合入main，不推送。
- 浏览器实测发现直接访问 ImageDescriptor 尺寸在Web不支持，已改用共享 instantiateImageCodecWithSize 接口；对应codec3项复测与静态分析通过，Web debug重建完成。
- Edge检查完成实际图片载入、量化预览、真实下载（172832B，720×480，格式0）及转入Film后模拟发送完成。自动化原生文件选择器事件不稳定，最终探针对真实file input注入测试文件；未把这部分当作系统选择器验收。后续按钮按可见位置操作，页面无脚本异常。预览截图和下载文件保存在忽略目录 .output/ark/flutter-preview/。
### 阶段 C6：纠正网页功能移植口径（2026-10-09）

- 用户明确要求按原 ForFilm 网页功能移植重构，并保留当前新功能。C5 的 Frame/Film 分工与简化算法不符合该要求，本阶段修正；完整迁移清单见 flutter_app/README.md 的“以 ForFilm 为基准的迁移状态”。
- Frame 四入口、拾光/定影自动转换、一言在线和自定义、批量队列；Film 恢复上传/算法/预览/输出，保留文件直传附加区。已有OTA和通行证等未删除。
- Atkinson增强使用原LUT，七种基础算法以原JS fixture对照；异步逐行保留取消/进度。自适应、扩展色、SZ、动画、文件管理等未完成项明确登记，不能以近似或占位当作完成。
- 仅Web构建；Android相机/多选桥接只做源码检查，不编译APK。当前批量通过现有WiFi逐张发送，和旧BLE静默批量仍有差异。
- 最小验证：源算法对照及codec 5项、快捷入口/片单2项、编辑失败与状态保留2项、既有Film/OTA状态隔离9项通过。测试中的滚动定位改为指定外层页面滚动容器，避免新增编辑器文本框内部Scrollable干扰；产品状态断言保留。
- Web实际联调：拾光自动增强转换与Frame页内模拟发送通过；Film图片导入、自动原色预览、真实下载通过（172832B，文件头720×480）。初次发现浏览器对独立LUT资源返回204空体，随后按原网页方式将原表随Dart源码携带，问题消失。文件输入自动化采用注入测试照片；Web相机权限/真实拍照、在线一言可用性和Android输入留待对应环境验收。
- 最终Web debug构建和受影响文件静态检查通过。没有编译/安装APK、没有改写设备或推送远端。后续工作以迁移清单补差为准，不能宣称网页全部功能已等价完成。

### 阶段 C7：ForFilm 余项与主题（2026-10-09）

- 在既有 Frame/Film 分工上补入自适应、46 色 ColorQual、55 色 ColorFast 与 SZ 增强算法；多帧动画工坊支持素材、逐帧编辑、预览及 film 输出；设备文件页提供目录切换、显示与删除操作。具体范围和差异见 `flutter_app/README.md` 的 C7 迁移矩阵。
- ForFilm 与 Ark 主题已做跨页面控件与导航统一，并在 Web 预览中检查；Flutter 字体、原生控件和实体屏幕效果尚不能宣称与原网页逐像素一致。本阶段只构建 Web debug，新增 Android 桥接未集中编译或安装验证。

### 阶段 C8：设置布局、关于与 App 更新（2026-10-09，本地实现）

- 连接信息移到连接页；设置页底部为界面主题。关于页显示 Rafael-ban 署名、GitHub 作者主页、项目源码、反馈入口和 GPL-3.0 许可。
- App 更新查询公开 GitHub Releases 中精确命名的 `framefilm-ark-flutter.apk` 与 `framefilm-ark-flutter.json`，读取元数据后按 `versionCode` 比较当前安装包。Android 端实现字节进度、取消、失败重试、下载校验和系统安装器入口；Web 只查询或在演示模式模拟。App 更新独立于 Ark 固件 OTA，后者仍按实际 ELF 指纹确认。
- 手动发布同时要求固件和 Android job 成功，附件包含原始 Ark 应用镜像、APK 和 JSON。为保证覆盖更新签名一致，发布前必须配置 `ARK_DEBUG_KEYSTORE_BASE64` 指向现有 App 的临时 debug keystore；缺少时发布构建失败。
- 本轮尚未推送、运行远端 CI、发布 Release、编译新增 Android 原生代码或安装真机；下载、签名兼容和系统安装流程仍待对应环境验收。此前已通过的旧版 APK/OTA 实机记录不代表 C8 新流程通过。
- C8 最小验证：新增 Dart 静态分析通过；发布筛选/无候选/网页版只读 3 项与设置导航 1 项通过；Web debug 构建完成。Edge 已确认连接信息位置、主题位于设置底部、Rafael-ban 作者主页跳转和 390px 布局；真实 GitHub 查询返回近 20 个正式发布中暂无可用 Flutter APK。CI YAML 与发布脚本语法检查通过，未将这些结果视作 Android 安装验收。

### 阶段 C8 交付与安装准备（2026-10-09）

- 按用户调整，设置页底部顺序为界面主题 → 应用更新 → 关于；署名为 App 开发维护 · Rafael-ban。
- 用户授权推送，并在本地构建审批连续超时后选择 GitHub Actions 构建。本地未生成本次新 APK，后续以本次源码提交对应的 CI 产物为准。
- 手机已通过 ADB 检测：arm64，现有 0.3.0-dev；Release 包与 .dev 并存，不删除旧版数据。安装及运行结果待构建完成记录。
