# Ark LVGL 电脑模拟器

这是**真实 Ark 页面 C 源码**的桌面预览。CMake 直接编译固件中的开机、主菜单、设置、休眠卡、通行证、时钟页面，以及 `app_shell`、图标资源；LVGL 使用 Ark 已有的 `managed_components/lvgl__lvgl`，SDL2 负责打开电脑窗口。硬件数据由模拟器给出样例值，不模拟 ESP-IDF、电子纸物理旋转和刷新耗时。

在已配置 WSLg 的 Windows 电脑上，双击 `run.cmd`，或从 PowerShell 运行：

```powershell
./tools/ark-ui-simulator/run.ps1
```

WSL 中需要已有 GCC、CMake、pkg-config 和 SDL2 开发包；`pkg-config --modversion sdl2` 可检查 SDL2。脚本只在本目录的 `build/` 中生成文件，不安装全局依赖。Linux 有 SDL2 时也可直接运行：

```sh
cmake -S tools/ark-ui-simulator -B tools/ark-ui-simulator/build
cmake --build tools/ark-ui-simulator/build -j4
./tools/ark-ui-simulator/build/ark-ui-simulator
```

操作：↑/↓ 切换菜单项或设置行；Enter 进入页面、修改设置；双击 Enter 返回菜单并高亮刚离开的应用；按住 Enter 一秒预览休眠卡。数字 1–6 依次直接预览开机、菜单、设置、休眠卡、通行证、时钟。Esc 关闭窗口。图片、模板、动图走固件直绘路径，尚未接入此模拟器。

当前六个页面及状态栏的功能文案已适配中文，保留原版轮播、图标、反白卡片、切角与信息面板；设置页保留原 17 行单页布局。品牌、版本号和硬件标识保留。模拟器始终链接最新页面源码，通行证内容仍是开发中的占位页。

模拟器把 LVGL 的内存分配接到桌面 C `malloc`，供快速切页预览；固件使用自己的 ESP-IDF heap 配置。两者的可用内存和分配失败行为不能互推。

使用的是 [LVGL 官方 SDL 桌面驱动](https://docs.lvgl.io/master/integration/pc/sdl.html)。逻辑窗口为 480×720，符合 Ark `ui_conf.h`；实际电子纸仍需上机验证。

本机中文页面版 `--smoke` 已通过：验证菜单选通行证、双击返回后仍选中原图标并再次进入，设置修改、设置及时钟返回索引，以及休眠卡唤醒。截图在项目根目录 `.output/ark/logs/full-cn-preview/`：`boot.bmp`、`menu.bmp`、`menu-return.bmp`、`pass.bmp`、`settings.bmp`、`clock.bmp`、`sleep.bmp`。这些截图验证页面渲染与最小导航，不代表实体屏幕验收。模拟器目前长按统一展示休眠卡，不能据此判断实机 app 内休眠的留屏行为；固件 app 内休眠会保留当前画面。
