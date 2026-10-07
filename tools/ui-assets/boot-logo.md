# Ark 开机 / 休眠 logo

`boot_logo_source.svg` 是用户提供的 `罗德岛Logo.svg` 的原样副本，外部原文件未改动。开机页和休眠卡均使用这份塔楼徽标，替换此前 BMP 徽标。

运行 `node tools/ui-assets/gen_boot_logo.cjs` 生成 `boot_logo_mono.png` 和 `film_ui/src/ui_boot_logo.c`。需要 Node.js 和 sharp；只使用已生成的固件资源进行构建时不需要运行转换器。

- 保留塔楼、三角边饰、文字与图形内部留白；将原青色绘制为黑色，透明处合成白底，不删除路径或修改轮廓。
- 保持原有 256px 高度，按 SVG 比例等比放入 304×256 资源（宽度按 I1 字节对齐），不在 LVGL 中二次缩放。
- 当前 UI 使用 I1 黑白快刷，不显示原青色。先以 144 DPI 栅格化 SVG，再缩小到实际尺寸，黑白阈值取 210，保留斜向细笔画，避免原 160 阈值将小字过滤为断点；不使用灰阶抖动。用户选择保持 304×256，不放大徽标；细小装饰文字仍受实际像素数量限制，模拟器不能代替实体屏幕验收。
- 资源为 LVGL I1，含调色板共 9736 字节，开机页和休眠卡共用。

彩蛋每次页面创建抽取一次，总触发概率 10%；中文五句话等概率选择（每句 2%），英文四句话等概率选择（每句 2.5%）。加载进度不会重新抽取，未触发时留空。英文文案核对自 [英文游戏第 15 章加载彩蛋记录及截图](https://arknights.wiki.gg/wiki/Dissociative_Recombination/Glitches.log)，对应中文第 2–5 句；第 1 句未找到可靠的英文游戏原文，用户选择英文暂用其余四句，不自行翻译或标为官方。

模拟器可用 `ARK_BOOT_EGG=0` 关闭彩蛋、`1`–`5` 按中文编号强制选句（英文 1 留空），此环境变量不进入固件实现。`--prts-preview` 生成中文 12 张、英文 10 张开机/休眠截图，加中英文菜单共 24 张。

## 开机页英文标题

仅开机页的 `ARKNIGHTS` 和 `PRTS SYNTHESIZE INFORMATION ANALYSIS OS` 使用 Novecento Sans Wide：前者 DemiBold 48px，后者 Normal 14px，均将排好的文字图形向右倾斜 12°（非字体原生 Italic 字款）。两句为固定尺寸的黑白图片，不是可重新排字的字库，其他页面和动态文本字体不变。

来源：[Synthview 字体介绍](https://typography.synthview.com/novecento-sans-font-family.php)、[作者在 DaFont 发布的字体](https://www.dafont.com/novecento-sans.font)。作者的 [免费版 EULA](https://typography.synthview.com/pdf/eula-free-font-dafont.pdf) 允许制作固定尺寸的文字图形，限制字体软件的转换、分发和软件嵌入。因此原始 OTF 不进入仓库或固件，全局字库替换暂不实施；用户已选择先预览两句标题。

复现：取得作者提供的字体后，运行 `python tools/ui-assets/gen_boot_typography.py <字体目录>`。脚本只绘制这两句固定内容，生成 `ui_boot_typography.c`；不生成通用字符表。
