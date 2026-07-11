# Face Access System — 智能人脸门禁系统

基于 ESP32-P4 的边缘 AI 人脸识别门禁方案。支持本地人脸检测/特征提取/1:N 识别、PIR 人体感应自动唤醒、7 寸触摸屏交互、MQTT 远程管理、OTA 固件升级。

---

## 硬件选型

| 类别 | 型号 | 接口 | 关键参数 |
|------|------|------|---------|
| 主控 | ESP32-P4NRW32 | — | RISC-V 双核 400MHz, 16MB Flash, 32MB PSRAM |
| 摄像头 | OV5647 | MIPI CSI-2 (2-lane) | 800×640 RGB888, V4L2 驱动 |
| 显示屏 | Raspberry Pi 7" Touch | MIPI DSI (1-lane 600Mbps) | 800×480, TC358762 DSI→RGB 桥 + ATTINY88 背光 |
| 触摸 | FT5406 | I²C (0x38) | 电容式, 10 点触控 |
| 人体感应 | HC-SR501 | GPIO 中断 (双沿) | PIR 红外, 检测距离 3~7m |
| 继电器 | SRD-05VDC ×3 | GPIO (高电平吸合) | 门锁 / 报警 / 韦根电源 |
| 音频 | ES8311 + NS4150B | I²S + I²C (0x18) | 16kHz 采样, 立体声 |
| 以太网 | IP101 | RMII | 100Mbps, 板载 RJ45 (POE) |
| 存储 | MicroSD | SDMMC 4-bit | FATFS, 人员库 + 通行记录 |

## 软件架构

![](/home/xiamu/下载/软件架构设计.png)

**设计原则**：
- BSP → DAL → Service 三层严格分层，依赖方向单向
- 编译期注入 (create + ops->ctx)，零运行时开销
- 共享帧模型：camera 帧经 buffer_pool (PSRAM) 零拷贝分发

## 核心功能

| 功能 | 描述 |
|------|------|
| **人脸识别** | 1:N 实时匹配，MSR 粗检测 → MNP 精检测 → MobileFaceNet 512 维特征 → 余弦相似度 |
| **PIR 唤醒** | 人体靠近自动开启识别，离开后延时关闭，低功耗 |
| **人脸注册** | 固定圆形区域裁剪 + 多帧均值特征，3~4 秒完成 |
| **触摸交互** | 7" 屏 LVGL 界面，管理员密码/注册/删除/日志/改密 |
| **远程管理** | MQTT 云端通信，支持远程开门、日志上报、模型 OTA |
| **门锁控制** | 继电器脉冲控制，识别成功自动开门 |
| **识别反馈** | 预览屏底部弹框显示结果 + 遮名保护隐私 |
| **数据存储** | PSRAM 缓存 + SD 卡 FATFS 持久化，空闲时批量写入 |
| **固件升级** | OTA 双槽位，支持固件与 AI 模型独立更新 |

## 快速开始

### 环境要求

- Ubuntu 22.04+ / macOS
- ESP-IDF v5.5
- Python 3.12
- VS Code + PlatformIO (推荐)

### 编译

```bash
cd software_engineering/firmware
idf.py build
```

### 烧录

```bash
idf.py -p /dev/ttyUSB0 flash
```

### 分区布局 (16MB Flash)

| 分区 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| NVS | 0x9000 | 24KB | 系统配置 |
| otadata | 0x10000 | 8KB | OTA 槽位信息 |
| ota_0 | 0x20000 | 4MB | 固件 A |
| ota_1 | 0x420000 | 4MB | 固件 B |
| human_face_det | 0x820000 | 2MB | 人脸检测模型 |
| human_face_feat | 0xA20000 | 2MB | 特征提取模型 |
| user_nvs | 0xC20000 | 384KB | 用户数据 (预留) |

## 技术栈

| 层面 | 技术 |
|------|------|
| 主控 | ESP32-P4 (RISC-V 双核 400MHz) |
| OS | FreeRTOS (ESP-IDF) |
| AI 推理 | [esp-dl](https://github.com/espressif/esp-dl) (ESP32-P4 PIE 指令加速) |
| 界面 | LVGL v9 (DIRECT 双缓冲, 800×480 RGB888) |
| 摄像头 | V4L2 + USB UVC (OV5647) |
| 显示屏 | MIPI DSI → TC358762 RGB 桥 |
| 存储 | MicroSD FATFS (人员特征 + 通行记录) |
| 网络 | Ethernet (IP101 RMII) + WiFi (ESP32-C6 SDIO) |
| 云端通信 | MQTT (Mosquitto) |
| Web 后台 | FastAPI + Vue 3 (Element Plus) |
| OTA | ESP-IDF OTA + 分区表双槽位 |

## 文档索引

| 文档 | 内容 |
|------|------|
| [开发手册](Doucments/开发手册.md) | 固件分层架构、开发范式、目录约定 |
| [软件架构设计](Doucments/软件架构设计.md) | 整体架构、模块划分、接口契约 |
| [硬件资源与接口映射](Doucments/硬件资源与接口映射.md) | GPIO 分配、I²C 地址、DSI 配置 |
| [数据流与控制流分析](Doucments/数据流与控制流分析.md) | 帧数据流、事件流、PIR 状态机 |
| [Flash与PSRAM内存分配设计](Doucments/Flash与PSRAM内存分配设计.md) | 16MB Flash + 32MB PSRAM 布局 |
| [V4L2框架](Doucments/V4L2框架.md) | USB UVC V4L2 驱动机制 |
| [自定义人脸特征提取模型部署](Doucments/自定义人脸特征提取模型部署.md) | eature_model 模型部署 |

## License

MIT
