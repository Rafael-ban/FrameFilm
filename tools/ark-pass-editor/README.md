# Ark 通行证编辑器

独立本地静态网页。界面采用罗德岛终端的黑白干员档案语汇：档案标题、照片、代号/编号栏、所属与签名。资料默认为空，不预设任何人物身份。

## 使用

在此目录运行 `python -m http.server 8000`，用支持 Web Bluetooth 的浏览器打开 `http://localhost:8000/`。填写资料后可下载 `profile.bin`、1bit PNG 预览和可继续编辑的 `profile.json`。页面自动尝试存一份本地草稿；换浏览器或清理浏览器数据前请导出 JSON。蓝牙由用户点击「连接 Ark」后手动选择 `FRAMEFILMARK`。连接后可点击「读取设备资料（替换当前草稿）」；读取会覆盖正在编辑的资料，因此请先导出需要留存的草稿。

选择头像时按中心裁剪为 **128×160**，保持比例，再编码为 PNG data URL。导入/导出的 JSON 只包含这张缩图，不保存原始照片。头像可随时更换或移除。代号、编号需填写或明确勾选「尚未设置」；所有文字按实际绘制宽度校验，签名最多三行。字段有误时不能导出位图或发送。

## 档案数据

`profile.json` 是 UTF-8 JSON。v1 schema 固定为：

```json
{
  "version": 1,
  "codename": "",
  "codenameUnset": false,
  "number": "",
  "numberUnset": false,
  "affiliation": "",
  "signature": "",
  "avatar": null
}
```

`avatar` 为 `null` 或 **128×160 PNG 的 data URL** (`data:image/png;base64,...`)。其他字段全部为字符串；两个 `Unset` 字段为布尔值。桌面/手机客户端可读写此格式。导入会验证版本、类型、长度和头像尺寸。JSON 可保存未完成的草稿；设备发送需要通过布局校验。

## 主体图与设备文件

画布精确为 **440×608**。上方状态栏 30 px、下方提示行 28 px 由设备固件绘制，编辑器不画。布局：顶部中文档案标题、左 128×160 头像与右侧代号/编号、所属、最多三行签名、底部档案索引。功能标签全部使用中文；仅保留 `RHODES ISLAND` 与 `ARKNIGHTS` 作为品牌文字。浏览器使用本地 CJK 字体在 Canvas 绘制，整图以亮度 160 阈值转成黑白；预览和导出的像素完全一致，实际字形取决于本机字体。

`profile.bin` 大小为 **33,456 B**：16 B `FFUI` 头（version=1、format=1、宽 440 与高 608 为 little-endian，余字节 0），随后每行 55 B，MSB first，1=黑、0=白。

蓝牙使用 ForFilm 的 `0x2000/0x2001` UUID 和 `0x55 CH LEN DATA SUM` 帧。每个文件按 START(0x03) → NAME(0x00，ASCII 路径含末尾 `\0`) → LEN(0x01，4 B 大端) → DATA(0x02，192 B 分片) → STOP(0x04) 发送；控制帧间 50 ms，数据帧间 4 ms（与现有 ForFilm 的 2+2 ms 相同）。现有 ForFilm 上传函数发送文件名时未显式附加 `\0`；Ark 固件接收端按 LEN 复制并补零，因此两种都能解析，此工具按项目字符串规范显式发送零结尾。先发送 `app/pass/profile.bin`，再发送 `app/pass/profile.json`。发送期间锁定资料编辑，确保文件对应点击发送时的画面。取消或中断时断开连接，并保留已发送 bin / 未确认 JSON 的状态；重新连接后从头发送。设备协议没有文件保存 ACK，页面只报告「发送完成」；请在设备上按确认加载。

读取需要支持 **0x53** 的新版 Ark 固件。连接后同一 BLE characteristic 启用通知，一次只请求一块：请求 `offset`（uint32 BE）+ `count`（1..128）；响应为 `status`（1 B）+ `offset`（4 B）+ `total`（4 B）+ 数据（0..128 B）。`status` 依次为 0 成功、1 未配置、2 读取失败、3 忙、4 参数错误。页面验证帧头、通道、长度、校验和、offset 与 350000 B 上限，按 offset 拼齐 UTF-8 JSON，再校验 schema 与头像；全部成功后才替换表单和本地草稿。每块等待 7 秒，超时会提示可能需要新版固件。取消、断连、解析失败都保留当前编辑内容；读取和上传不能同时执行。
