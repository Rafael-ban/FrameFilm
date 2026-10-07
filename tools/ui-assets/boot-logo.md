# Ark 开机 / 休眠 logo

`boot_logo_source.png` 是用户提供的 `开机logo.bmp` 的无损 PNG 副本，原 BMP 未改动。`PRTS终端.jpg` 用作文案参考，不作为另一张固件位图。

运行 `python tools/ui-assets/gen_boot_logo.py` 生成 `boot_logo_mono.png` 和 `film_ui/src/ui_boot_logo.c`。需要 Pillow。

- 保留完整原图的菱形、RHODES ISLAND、灰色 PRTS 底纹和原边距；按用户要求整体反色为白底黑色徽标，底纹一并反色，不删元素、不重新绘制。
- 按显示尺寸 256×256 等比缩放，Floyd–Steinberg 误差扩散到黑白，不在 LVGL 中二次缩放。
- 当前 UI 显示链路是 I1 黑白快刷，无法直接保留连续灰阶。PRTS 灰色仍需要黑白点近似；模拟器与实体电子纸的对比度和刷新效果不能等同。
- 资源为 LVGL I1，含调色板共 8200 字节，开机页和休眠卡共用。

彩蛋每次页面创建抽取一次，总触发概率 10%，触发时五句话等概率选择；加载进度不会重新抽取。未触发时该区域留空，中英文模式均使用用户指定的中文原句。模拟器可用 `ARK_BOOT_EGG=0` 关闭彩蛋、`1`–`5` 强制选句，此环境变量不进入固件实现。
