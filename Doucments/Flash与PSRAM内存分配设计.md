# Flash 与 PSRAM 内存分配设计

## 概述

ESP32-P4 的内存架构分为三层：

| 层级 | 容量 | 速度 | 用途 |
|------|------|------|------|
| **Internal SRAM** | ~768KB | 最快 (CPU 直连) | 栈、LVGL 绘制缓冲、RTOS 内核对象、热代码 |
| **External PSRAM** | 32MB | 中等 (Octal SPI, 200MHz) | 大块数据：帧缓冲、CNN 模型、特征库、动态堆 |
| **External Flash** | 16MB | 慢 (Quad SPI, 40MHz) | 固件、CNN 模型持久化、NVS、OTA 双槽 |

核心原则：**SRAM 放热数据（高频访问、小尺寸），PSRAM 放大块数据（低频访问、大尺寸），Flash 放持久化数据。**

---

## 1 Flash 分配 (16MB)

```
0x000000 ┌──────────────────────────┐
         │ Bootloader               │  28KB    只读, 不可变
0x008000 ├──────────────────────────┤
         │ Partition Table          │  4KB     只读
0x009000 ├──────────────────────────┤
         │ NVS (系统配置)            │  24KB    R/W, OTA 后保留
0x00F000 ├──────────────────────────┤
         │ PHY Init                 │  4KB     只读, RF 校准数据
0x010000 ├──────────────────────────┤
         │ OTA Data                 │  8KB     R/W, 活跃槽位/启动计数
0x012000 ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┤  56KB    对齐空隙 (内碎片)
0x020000 ├──────────────────────────┤
         │                          │
         │ ota_0 (固件 A)           │  3MB     只读 (OTA 时写)
         │  ├─ .text (代码)          │
         │  ├─ .rodata (常量)        │
         │  ├─ .data (初始化数据)     │
         │  └─ .bss  (零初始化)      │
         │                          │
0x320000 ├──────────────────────────┤
         │ ota_1 (固件 B)           │  3MB     只读 (OTA 写入目标)
0x620000 ├──────────────────────────┤
         │                          │
         │ models (SPIFFS, 4MB)     │  R/W     可独立更新
         │  ├─ detect.bin           │  ~500KB   人脸检测模型
         │  ├─ feature.bin          │  ~1.5MB   特征提取模型
         │  └─ (预留)               │  ~2MB
         │                          │
0xA20000 ├──────────────────────────┤
         │ user_nvs                 │  384KB   R/W, 用户配置备份
0xA80000 ├──────────────────────────┤
         │ (空闲)                   │  5.5MB   预留
0x1000000└──────────────────────────┘
```

### Flash 容量分析

| 分区 | 大小 | 已用 | 余量 |
|------|------|------|------|
| ota_0 / ota_1 | 3MB × 2 | 1.2MB/槽 | 1.8MB/槽 (60%) |
| models (SPIFFS) | 4MB | ~2MB | 2MB (50%) |
| user_nvs | 384KB | ~4KB | 380KB |
| 总空闲 (含未分配) | 7.25MB | — | 无限扩展 |

---

## 2 PSRAM 分配 (32MB)

### 2.1 总览

```
32MB PSRAM (0x02000000 ~ 0x04000000, 虚拟地址)
│
├── [Zone 0] V4L2 帧缓冲池          ~3.5MB    DMA 可访问, 固定分配
├── [Zone 1] CNN 模型权重            ~3MB      从 Flash SPIFFS 加载
├── [Zone 2] 人脸特征数据库           ~2.1MB    1000 用户 × 512 float
├── [Zone 3] 采集帧中转缓冲           ~3.5MB    人脸检测/特征提取的帧副本
├── [Zone 4] 人脸裁剪缓冲池          ~300KB    112×112×3 × 5 张脸
├── [Zone 5] 特征向量暂存            ~10KB     512 float × 5 张脸
├── [Zone 6] LVGL 图像 canvas       ~1.15MB   相机预览帧映射
├── [Zone 7] 系统堆 (heap)           ~剩余    动态分配: 队列缓冲/日志/临时对象
└──────────────────────────────────────────
```

### 2.2 详细分区

```
PSRAM 地址空间 (32MB = 0x02000000)

Zone 0: V4L2 帧缓冲池 (INIT 时分配, 常驻, DMA 可访问)
┌──────────────────────────────────────┐  ~3.5MB
│ fb[0]: 800×480×3 = 1,152,000 B      │  单帧 RGB888
│ fb[1]: 800×480×3 = 1,152,000 B      │
│ fb[2]: 800×480×3 = 1,152,000 B      │
│ fb_count = 3 (BOARD_CAMERA_BUFFER_COUNT) │
│ 分配方式: heap_caps_malloc(SPIRAM|DMA)   │
│ 或 V4L2 MMAP 内核分配 (穿透 PSRAM)        │
└──────────────────────────────────────┘

Zone 1: CNN 模型权重 (INIT 时从 Flash 加载, 常驻)
┌──────────────────────────────────────┐  ~3MB
│ detect_model:  ~500KB               │  人脸检测 (HumanFaceDetect)
│ feature_model: ~1.5MB               │  特征提取 (MobileFaceNet/ResNet)
│ 预留:          ~1MB                 │  活体检测/口罩检测等
│ 分配方式: heap_caps_malloc(SPIRAM)  │
│ 加载: esp_partition_mmap()→memcpy   │
└──────────────────────────────────────┘

Zone 2: 人脸特征数据库 (运行时常驻, N_max=1000 用户)
┌──────────────────────────────────────┐  ~2.1MB
│ feature_db_t {                       │
│   user_ids[1000]  = 1000 × 4B = 4KB │
│   user_names[1000][32] = 32KB        │
│   vectors[1000][512] = 2,048,000 B  │  1000 × 512 × 4B = 2MB
│   user_count      = 2B              │
│ }                                    │
│ 分配方式: heap_caps_calloc(SPIRAM)  │
│ 加载: SD card → db_store_load_all() │
│ 存储: SD 主存储 + PSRAM 热缓存       │
│       NVS 仅存索引(user_id 列表)     │
└──────────────────────────────────────┘

Zone 3: 采集帧中转缓冲 (运行时, 按需分配/释放)
┌──────────────────────────────────────┐  ~3.5MB
│ 用途: 当 V4L2 帧需要被多个消费者    │
│ 持有超过采集周期时, 拷贝到此处      │
│ (通常零拷贝模式不需要, 兜底用)       │
│ 分配方式: heap_caps_calloc(SPIRAM)  │
└──────────────────────────────────────┘

Zone 4: 人脸裁剪缓冲池 (运行时, 5 张脸并发)
┌──────────────────────────────────────┐  ~300KB
│ face_crop[0]: 112×112×3 = 37,632 B  │
│ face_crop[1]: 112×112×3             │
│ face_crop[2]: 112×112×3             │
│ face_crop[3]: 112×112×3             │
│ face_crop[4]: 112×112×3             │
│ 分配方式: heap_caps_malloc(SPIRAM|DMA) │
│ (可能需要 DMA 给 NPU/ISP)            │
└──────────────────────────────────────┘

Zone 5: 特征向量暂存 (栈/堆, 临时)
┌──────────────────────────────────────┐  ~10KB
│ face_feature_t feat[5]               │  5 × 512 × 4B = 10KB
│ 分配方式: 栈上或 heap_caps_malloc   │
└──────────────────────────────────────┘

Zone 6: LVGL 预览帧映射 (零拷贝, 指针引用)
┌──────────────────────────────────────┐  ~1.15MB (不额外分配)
│ 指向 Zone 0 的 fb[i], 不是新内存    │  仅持有指针
│ lv_image_dsc_t.data = fb[i]         │
└──────────────────────────────────────┘

Zone 7: 系统动态堆 (剩余空间)
┌──────────────────────────────────────┐  ~20MB
│ FreeRTOS 队列缓冲                   │
│ MQTT 收发缓冲 (TCP window)           │
│ log_sink 环形缓冲                    │
│ JSON 解析/序列化临时内存            │
│ LVGL 临时渲染对象                    │
│ db_store 序列化缓冲                  │
│ SD card FATFS 缓存                   │
│ OTA 下载缓冲                        │
└──────────────────────────────────────┘
```

### 2.3 PSRAM 容量分析

| Zone | 用途 | 大小 | 占比 | 生命周期 | 分配方式 |
|------|------|------|------|----------|----------|
| 0 | V4L2 帧缓冲 | 3.5MB | 11% | init→deinit | `SPIRAM \| DMA` |
| 1 | CNN 模型 | 3.0MB | 9% | init→deinit | `SPIRAM` |
| 2 | 特征数据库 | 2.1MB | 7% | init→deinit | `SPIRAM` |
| 3 | 帧中转缓冲 | 3.5MB | 11% | 按需 | `SPIRAM` |
| 4 | 人脸裁剪池 | 0.3MB | 1% | 按需 | `SPIRAM \| DMA` |
| 5 | 特征暂存 | ~10KB | <1% | 临时 | 栈/堆 |
| 6 | LVGL 预览 | — | 0% | 零拷贝引用 | 无分配 |
| 7 | 系统堆 | ~18.5MB| 58% | 持续 | `SPIRAM` |
| **合计** | | **~31MB** | **97%** | | |

---

## 3 Internal SRAM 分配 (~768KB)

```
Internal SRAM (ESP32-P4):
┌──────────────────────────────────────┐
│ 中断向量表 + 启动代码                 │  ~4KB    只读
├──────────────────────────────────────┤
│ .data / .bss (全局变量)              │  ~150KB   R/W
│  ├─ s_draw_buf_1: 800×40×3 = 96KB   │  LVGL 双缓冲
│  ├─ s_draw_buf_2: 800×40×3 = 96KB   │
│  ├─ s_in_flight[4]: 4 × ~40B        │  camera 在途帧跟踪
│  ├─ s_preview_img_dsc: ~30B         │  LVGL 图像描述符
│  └─ 各 service 静态变量             │
├──────────────────────────────────────┤
│ 任务栈 (TCB + Stack)                 │  ~130KB
│  ├─ svc_wdt:    4KB                  │
│  ├─ svc_camera: 8KB                  │
│  ├─ svc_touch:  4KB                  │
│  ├─ svc_ui:     12KB (含 LVGL 临时对象) │
│  ├─ svc_face_*: 3 × 8KB             │
│  ├─ svc_perm:   4KB                  │
│  ├─ svc_mqtt:   6KB                  │
│  └─ main:       8KB                  │
├──────────────────────────────────────┤
│ FreeRTOS 内核对象                     │  ~30KB
│  ├─ 12 队列 TCB + 存储              │
│  ├─ 信号量/Mutex/EventGroup         │
│  └─ 定时器                           │
├──────────────────────────────────────┤
│ 驱动 DMA 描述符                       │  ~10KB
│  ├─ I2C DMA                          │
│  ├─ SDMMC DMA                        │
│  └─ SPI DMA                          │
├──────────────────────────────────────┤
│ 驱动内部缓冲                          │  ~20KB
│  ├─ WiFi/MAC 缓冲 (若启用)           │
│  ├─ ESP-NETIF LwIP pool             │
│  └─ ESP-TLS mbedTLS 上下文          │
├──────────────────────────────────────┤
│ 系统保留 + IDLE 栈                   │  ~20KB
├──────────────────────────────────────┤
│ DRAM 堆 (malloc/calloc)              │  ~剩余  小型临时分配
└──────────────────────────────────────┘
```

### SRAM 容量分析

| 分类 | 大小 | 占比 |
|------|------|------|
| 代码/数据段 (.data/.bss) | ~250KB | 33% |
| 任务栈 | ~130KB | 17% |
| RTOS 内核 | ~30KB | 4% |
| 驱动 | ~30KB | 4% |
| 堆 (剩余) | ~328KB | 42% |

---

## 4 内存分配策略

### 4.1 分配接口选择

```c
// 大块、常驻、DMA 需求 (帧缓冲、裁剪缓冲)
void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

// 大块、常驻、无 DMA 需求 (CNN 模型、特征库)
void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// 小块、频繁分配释放 (队列消息、临时变量)
void *buf = malloc(size);  // 默认从 DRAM 堆, 不足时回退 PSRAM

// 极小、高频 (栈上)
uint8_t buf[256];

// Flash 直接映射 (只读, 零拷贝)
esp_partition_mmap(partition, offset, size, SPI_FLASH_MMAP_DATA, &ptr, &handle);
```

### 4.2 生命周期管理

```
INIT 阶段 (开机一次性):
  Heap Caps Alloc (SPIRAM | DMA) → V4L2 frame buffers [Zone 0]
  esp_partition_mmap(models)     → memcpy → Heap Caps Alloc (SPIRAM) [Zone 1]
  db_store_get_all()             → Heap Caps Alloc (SPIRAM) [Zone 2]

RUNNING 阶段 (按需):
  face_crop buffer               → Calloc (SPIRAM | DMA) → 用完 Free [Zone 4]
  feature vector temp            → 栈上 [Zone 5]
  queue message                  → 栈上或 DRAM heap

DEINIT 阶段 (关机/重启):
  heap_caps_free 按 Zone 逆序释放
  esp_partition_munmap
```

### 4.3 OOM (Out of Memory) 防护

```c
// 关键分配失败处理
void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
if (buf == NULL) {
    ESP_LOGE(TAG, "PSRAM OOM: %u bytes", size);
    // 1. 打印 heap_caps_get_free_size(MALLOC_CAP_SPIRAM) 诊断
    // 2. 降级: 减少 fb_count, 降低分辨率, 或使用 MMAP 代替 USERPTR
    // 3. 上报 mw_event_bus SERVICE_EVT_FAULT
    return DAL_ERR_NO_MEM;
}
```

### 4.4 内存监控

```c
// 周期性打印 (svc_wdt 可扩展)
void mem_diag(void) {
    ESP_LOGI("MEM", "DRAM  free: %u / %u",
        heap_caps_get_free_size(MALLOC_CAP_8BIT),
        heap_caps_get_total_size(MALLOC_CAP_8BIT));
    ESP_LOGI("MEM", "PSRAM free: %u / %u",
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI("MEM", "DMA   free: %u",
        heap_caps_get_free_size(MALLOC_CAP_DMA));
}
```

---

## 5 关键数据流的内存视角

### 5.1 摄像头帧流转

```
V4L2 驱动 (内核)                    Userspace (PSRAM)
     │                                    │
     │ DMA write ─────────────────────────▶│ fb[0] (1.15MB, Zone 0)
     │                                    │
     │ DQBUF                              │
     │  frame_handle = index+1 ──────────▶│ s_in_flight[slot] (SRAM, ~40B)
     │                                    │
     │                              svc_camera dispatch:
     │                               frame_buffer_t {  (SRAM, 栈)
     │                                 .buffer = fb[0]  // 指针, 零拷贝
     │                               }
     │                               xQueueSend(g_q_cam_to_ui)
     │                                    │
     │                              svc_ui:
     │                               lv_image_dsc_t.data = fb[0]  // 零拷贝
     │                               lv_timer_handler() → flush → draw_bitmap
     │                                    │
     │                               xQueueSend(g_q_ui_to_cam_frame_ret)
     │                                    │
     │ QBUF ◀────────────────────────── return_frame(fb[0])
```

> 关键：整个流转过程**零 memcpy**。fb[0] 始终在同一块 PSRAM DMA 缓冲中，仅通过指针在队列间传递。

### 5.2 CNN 推理的内存流转

```
Flash models SPIFFS (4MB)
     │
     │ esp_partition_mmap("models", ...)  // 直接映射, 零拷贝读
     ▼
PSRAM Zone 1 (~3MB)
     │ heap_caps_malloc(SPIRAM) → memcpy 模型权重
     │ face_engine_init(models_ptr)
     │
     ▼
PSRAM Zone 4 (人脸裁剪)
     │ detect 输出 face_box_t → 从 fb[0] 裁剪 112×112
     │ heap_caps_malloc(SPIRAM|DMA) → 对齐拷贝
     │                         (如果 NPU 需要 DMA, 必须对齐 + DMA capable)
     ▼
PSRAM Zone 5 (临时)
     │ face_engine_extract(crop_112) → face_feature_t (栈上, 512×4B=2KB)
     │
     ▼
PSRAM Zone 2 (比对)
     │ face_engine_compare(input_feat, &db.vectors[i])
     │ 遍历 1000 个用户, 每个 512-float 向量
     │ 总读取量: 1000 × 512 × 4B = 2MB
     │ 预计耗时: ~8-15ms (PSRAM 200MHz + ESP32-P4 SIMD)
```

### 5.3 触摸数据流转

```
FT5406 (I2C addr 0x38)
     │ I2C read 6B × 5 触点
     ▼
svc_touch 栈 (SRAM, ~50B)
     │ dal_touch_point_t pts[5]  (栈上)
     │ touch_msg_t msg = { .point = pts[i], .timestamp }  (栈上, 拷贝到队列)
     ▼
g_q_touch_to_ui 队列存储 (SRAM, sizeof(touch_msg_t) × 2 depth)
     │
     ▼
svc_ui indev read_cb (SRAM 栈)
     │ touch_msg_t msg  (栈上)
     │ data->point = msg.point
     │ data->state = PRESSED/RELEASED
```

> 触摸数据量极小（~10B/次），全程在 SRAM 的栈和队列存储中，不涉及 PSRAM。

### 5.4 特征库持久化流转（SD ↔ PSRAM）

```
SD Card (主存储)
  │
  │ db_store 文件布局:
  │   /data/face_db/
  │   ├── index.dat         ← NVS 同步的用户索引 (user_id 列表)
  │   ├── user_0001.bin     ← uint32_t id + char[32] name + float[512] vector
  │   ├── user_0002.bin
  │   └── ...
  │
  │ 开机加载: db_store_load_all()
  │   1. 读 NVS → 获取 user_count + user_id 列表 (快速验证)
  │   2. 遍历 SD /data/face_db/ → 反序列化每个 user_N.bin
  │   3. heap_caps_calloc(SPIRAM, user_count * sizeof(feature_entry_t))
  │   4. 全部加载到 PSRAM Zone 2 (1000 人 = 2MB, 约 200ms)
  │
  ▼
PSRAM Zone 2 (~2.1MB, 常驻)
  │
  │ 增删用户:
  │   添加: PSRAM 扩容 → SD 新写 user_N.bin → NVS 更新索引
  │   删除: PSRAM 标记删除 → SD 删文件 → NVS 更新索引
  │   (不重新加载全库, 增量操作)
  │
  │ 1:N 全量比对:
  │   for i in 0..user_count:
  │     face_engine_compare(input, &db.vectors[i]) → score
  │   1000 用户 = 2MB 向量数据, ~8-15ms
  │
  ▼
比对结果 → perm_manager 判决 → log_sink / MQTT / relay
```

### 5.5 1:N 全量实时比对分析

```
输入: 1 个 512-float 特征向量 (2KB, 栈上)
数据库: 1000 个 512-float 向量 (2MB, PSRAM Zone 2)

单次比对 (cosine similarity):
  dot = Σ(a[i] × b[i])    // 512 次乘加,  SIMD 加速 ~1µs
  na  = √Σa[i]²           // 预计算, 存入时算好
  nb  = √Σb[i]²           // 预计算, 存入时算好
  score = dot / (na × nb)

1000 次全量扫描:
  = 1000 × 1µs (SIMD dot) + 2MB PSRAM 读取 (~10ms)
  ≈ 12ms 总耗时

对比 camera 采集周期 (33ms @30fps 抽稀后 66ms):
  12ms << 66ms → 完全可以实时完成

优化方向 (如果需要 >1000 人):
  - 特征归一化预计算: 存入时存储 |v|=1 的归一化向量, 比对方只用 dot
  - 分段比对: 每 100 人一组, 找到 >threshold 即早停
  - PQ (Product Quantization): 压缩 512D→64D, 2MB→256KB
```

---

## 6 内存压力测试场景

| 场景 | PSRAM 压力 | 风险 |
|------|-----------|------|
| 正常采集 (3 fb × 1.15MB) | 3.5MB | 无 |
| 采集 + CNN 推理 | 6.5MB (fb + models + crop) | 无 |
| 采集 + CNN + 1000 用户 1:N | 8.6MB | 无 |
| OTA 下载 (1.2MB buffer) | 9.8MB | 无 |
| 新增用户 (PSRAM 扩容 + SD 写) | 8.7MB (临时 +2KB) | 无 |
| JSON 解析大 payload | +500KB 临时 | 低 |
| 所有子系统同时满载 | ~10.5MB | 无, ~21.5MB 空闲 |

| 场景 | PSRAM 压力 | 风险 |
|------|-----------|------|
| 正常采集 (3 fb × 1.15MB) | 3.5MB | 无 |
| 采集 + CNN 推理 | 6.5MB (fb + models + crop) | 无 |
| 采集 + CNN + 1000 用户 1:N | 8.6MB | 无 |
| OTA 下载 (1.2MB buffer) | 8.2MB | 无 |
| JSON 解析大 payload | +500KB 临时 | 低 |
| 所有子系统同时满载 | ~10MB | 无, 还有 ~22MB 空闲 |

---

## 7 配置宏对应

```c
// sdkconfig / Kconfig.projbuild 相关宏

// PSRAM
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y                     // ESP32-P4 Octal SPI PSRAM
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y               // heap_caps_malloc 支持 SPIRAM
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384    // <16KB 分配走 DRAM
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y // .bss 段可放 PSRAM (不推荐)

// Camera buffer
#define BOARD_CAMERA_BUFFER_COUNT  3          // V4L2 DMA 缓冲数量
#define FACE_CAM_FRAME_DIV         2          // 帧抽稀比

// LVGL draw buffer (DRAM)
#define UI_BUF_LINES  40
#define UI_BUF_SIZE   (800 * 40 * 3)          // 96KB × 2 = 192KB DRAM

// Feature DB
#define FACE_FEATURE_DIM   512u               // 特征维度
// 最大用户数由 PSRAM 容量决定: 1000 × 512 × 4B = 2MB

// Models
// detect.bin:  ~500KB (HumanFaceDetect)
// feature.bin: ~1.5MB (MobileFaceNet)
```
