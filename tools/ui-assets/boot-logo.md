# Ark 开机 / 休眠 logo

`boot_logo_source.png` 是用户提供的 `开机logo.bmp` 的无损 PNG 副本，原 BMP 未改动。`PRTS终端.jpg` 用作文案参考，不作为另一张固件位图。

运行 `python tools/ui-assets/gen_boot_logo.py` 生成 `boot_logo_mono.png` 和 `film_ui/src/ui_boot_logo.c`。需要 Pillow。

- 保留完整原图的菱形、RHODES ISLAND、灰色 PRTS 底纹和原边距；按用户要求整体反色为白底黑色徽标，底纹一并反色，不删元素、不重新绘制。
- 按显示尺寸 256×256 等比缩放，Floyd–Steinberg 误差扩散到黑白，不在 LVGL 中二次缩放。
- 当前 UI 显示链路是 I1 黑白快刷，无法直接保留连续灰阶。PRTS 灰色仍需要黑白点近似；模拟器与实体电子纸的对比度和刷新效果不能等同。
- 资源为 LVGL I1，含调色板共 8200 字节，开机页和休眠卡共用。

彩蛋每次页面创建抽取一次，总触发概率 10%；中文五句话等概率选择（每句 2%），英文四句话等概率选择（每句 2.5%）。加载进度不会重新抽取，未触发时留空。英文文案核对自 [英文游戏第 15 章加载彩蛋记录及截图](https://arknights.wiki.gg/wiki/Dissociative_Recombination/Glitches.log)，对应中文第 2–5 句；第 1 句未找到可靠的英文游戏原文，用户选择英文暂用其余四句，不自行翻译或标为官方。

模拟器可用 `ARK_BOOT_EGG=0` 关闭彩蛋、`1`–`5` 按中文编号强制选句（英文 1 留空），此环境变量不进入固件实现。`--prts-preview` 生成中文 12 张、英文 10 张开机/休眠截图，加中英文菜单共 24 张。

## 开机页英文标题

仅开机页的 `ARKNIGHTS` 和 `PRTS SYNTHESIZE INFORMATION ANALYSIS OS` 使用 Novecento Sans Wide：前者 DemiBold 48px，后者 Normal 14px，均将排好的文字图形向右倾斜 12°（非字体原生 Italic 字款）。两句为固定尺寸的黑白图片，不是可重新排字的字库，其他页面和动态文本字体不变。

来源：[Synthview 字体介绍](https://typography.synthview.com/novecento-sans-font-family.php)、[作者在 DaFont 发布的字体](https://www.dafont.com/novecento-sans.font)。作者的 [免费版 EULA](https://typography.synthview.com/pdf/eula-free-font-dafont.pdf) 允许制作固定尺寸的文字图形，限制字体软件的转换、分发和软件嵌入。因此原始 OTF 不进入仓库或固件，全局字库替换暂不实施；用户已选择先预览两句标题。

复现：取得作者提供的字体后，运行 `python tools/ui-assets/gen_boot_typography.py <字体目录>`。脚本只绘制这两句固定内容，生成 `ui_boot_typography.c`；不生成通用字符表。
