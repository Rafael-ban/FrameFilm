# FrameFilm 项目总览

> AI 开发辅助文档 — 提供项目全局视图，供 Codex / OpenCode / Trae 等工具自动引用。

## 项目定位

FrameFilm（帧影）是一款开源彩色电子纸冰箱贴，基于 ESP32-S3，通过 BLE 与手机通信显示照片。

- **主控**: ESP32-S3
- **固件框架**: ESP-IDF v5.5.2
- **许可证**: GPL-3.0
- **平台**: 嵌入式固件 + Web工具 + 微信小程序

## 三条固件线

| | 冰箱贴三机型 (frame_film) | 通行证版 (frame_film_ark) | 底座 (frame_film_dock) |
|---|---|---|---|
| 固件目录 | `firmware/frame_film/` | `firmware/frame_film_ark/` | `firmware/frame_film_dock/` |
| 定位 | 旧机型维护线 | **app 框架主线**（`film_app` + `film_ui`） | USB 复合设备（声卡 + HID + CDC） |
| 机型 | 三机型 STD / PRO / MAX（机型宏 + 屏幕宏） | **单机型**，硬件固定 | 单机型 |
| 屏幕 | E6 系列多款（`hal_epd.h` 的 `EPD_SELECT_E6_*`） | E6 3.70" 720×480（面板 `0x02`） | E6 3.64" 760×568（面板 `0x06`） |
| BLE 名 | — | `FRAMEFILMARK` | `FRAMEFILMDOCK` |
| 构建 | `cp sdkconfig_{std,pro,max} sdkconfig` 后 `idf.py build` | 直接 `idf.py build`（只有一份 `sdkconfig`） | 直接 `idf.py build` |

> 冰箱贴固件（`frame_film` 与 `frame_film_ark`）的 **BLE 协议完全同源**：同一套命令值与回包格式。app 框架（app 层 + LVGL UI 层）的新功能以 `frame_film_ark` 为主线，`frame_film` 不再跟进。

## 目录结构速览

```
FrameFilm/
├── firmware/                    # 固件
│   ├── frame_film/              #   冰箱贴三机型 (ESP-IDF)
│   │   ├── main/main.c          #     入口
│   │   └── components/
│   │       ├── film_sys/        #       系统层 (日志/NVS/配置)
│   │       ├── film_hal/        #       硬件抽象层 (EPD/电池/LED/SD/编码器)
│   │       ├── film_service/    #       服务层 (BLE/文件/OTA/WiFi/参数)
│   │       ├── film_app/        #       app 层 (框架核心/页面/内容 app)
│   │       └── film_ui/         #       LVGL UI 层
│   ├── frame_film_ark/          #   通行证版 单机型 (app 框架主线，结构同上)
│   └── frame_film_dock/         #   底座 单机型 (另含 USB 复合设备)
├── tools/                       # 客户端工具
│   ├── wechart/miniprogram/     #   微信小程序 (BLE + WiFi配网)
│   │   └── utils/
│   │       ├── ble-utils.js     #     BLE 协议实现
│   │       └── film-utils.js    #     film 文件生成
│   └── ForFilm/                 #   Web 工具 (Web Bluetooth API)
│       └── js/
│           ├── bluetooth.js     #     Web BLE 封装
│           └── convert.js       #     图片→film 转换
├── docs/                        # 文档
│   ├── blecmd/                  #   BLE 协议规范
│   ├── film/                    #   film 文件格式
│   ├── hardware/                #   硬件规格
│   ├── wifi/                    #   WiFi 功能说明
│   └── knowledge/               #   AI 开发知识库 (当前目录)
└── hardware/                    # 硬件设计 (PCB + 3D模型)
```

## 核心文件索引

### 必须了解的关键文件
| 文件 | 作用 |
|------|------|
| `firmware/*/components/film_service/inc/service_ble.h` | BLE 协议命令定义（所有通道常量） |
| `firmware/*/components/film_service/src/service_film.c` | film 播放核心逻辑 |
| `firmware/*/components/film_hal/src/hal_epd.c` | 电子纸驱动 |
| `firmware/*/main/main.c` | 固件入口 |
| `tools/wechart/miniprogram/utils/ble-utils.js` | 小程序 BLE 协议 |
| `tools/ForFilm/js/frame.js` | Web 端 BLE 协议 |
| `docs/blecmd/blecmd_protocol.md` | 协议完整文档 |

### 构建命令
```bash
# 冰箱贴三机型：先按机型拷贝 sdkconfig，再构建
cd firmware/frame_film && cp sdkconfig_std sdkconfig && idf.py build flash monitor

# 通行证版（单机型，无需拷贝）
cd firmware/frame_film_ark && idf.py build flash monitor

# 底座（单机型）
cd firmware/frame_film_dock && idf.py build flash monitor
```

## 关键技术概念

- **film 文件格式**: 32B 文件头 + 120000B 像素数据 (每字节 2 像素, 4bit/像素)
- **BLE GATT 协议**: 3 通道 (CH1 命令, CH2/CH3 数据), 包格式 `0x55 + CH + LEN + DATA + SUM`
- **颜色编码**: 6色 (黑/白/绿/蓝/红/黄), 通过 ColorTable 映射
- **存储**: SDNAND (默认) / TF 卡, FATFS 文件系统, SDMMC 模式
