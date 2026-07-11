/**
 * @file    main.c
 * @brief   Assembler — 启动、硬件 init、板级设备测试、Service 装配（依赖注入）
 *
 * @details 严格遵循架构规范生命周期顺序：
 *          1. NVS 初始化 + 事件循环 + netif
 *          2. board_v1_init() → 共享 I2C + 各 BSP create + DAL register（零硬件副作用）
 *          3. 各设备 ops->init(ctx, cfg) → 显式触发硬件初始化
 *          4. 板级设备测试（storage/display/camera/touch/relay/audio/network/pir）
 *          5. Middleware 初始化（event_bus / db_store；log_sink 由 svc_log 统一管）
 *          6. Service 启动（依赖注入，按依赖序）：svc_log → svc_wdt → svc_camera
 *             → svc_touch → face 三件套 → svc_ui → svc_perm_manager → svc_mqtt
 *          7. svc_camera_start（PIR 上升沿自动唤醒采集，开机显式启动一次）
 *          8. 喂狗尾循环
 *
 *          采集与 UI 预览已迁入 service 层：svc_camera 经 PIR 中断状态机驱动采集，
 *          帧经 buffer_pool(PSRAM) 零拷贝分发；svc_ui(LVGL) 消费帧渲染 canvas 预览。
 *          main 不再持有采集任务。
 *
 * @author  xiamu
 * @version 1.7
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "board_v1.h"
#include "board_v1_config.h"

/* DAL 管理 API（仅 Assembler 可含） */
#include "dal_camera.h"
#include "dal_display.h"
#include "dal_touch.h"
#include "dal_pir.h"
#include "dal_relay.h"
#include "dal_audio.h"
#include "dal_network.h"
#include "dal_err.h"

/* Middleware（Assembler 启动中间件） */
#include "mw_event_bus.h"
#include "db_store.h"
#include "msg_queues.h"
#include "esp_sntp.h"

/* Service（Assembler 注入依赖并启动） */
#include "svc_log.h"
#include "svc_wdt.h"
#include "svc_camera.h"
#include "svc_touch.h"
#include "svc_face_detect.h"
#include "svc_face_feature.h"   /* TODO: 恢复量化后启用 */
// #include "svc_face_identify.h"   /* TODO: 恢复量化后启用 */
#include "svc_ui.h"
#include "svc_perm_manager.h"
#include "svc_mqtt.h"
#include "svc_ota.h"
#include "esp_ota_ops.h"

#define ASM_TAG "APP_MAIN"
#define ST_TAG  "SELFTEST" /**< 仅编译期使用，selftest 已从 app_main 移除 */

/* ================================================================
 *  设备测试（原 board_selftest 逻辑内联）
 * ================================================================ */

/** 自检延时（保留 0-tick→1 保护） */
static inline void selftest_delay_ms(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    if (t == 0) t = 1;
    vTaskDelay(t);
}

/** 单项校验宏：执行并打印 PASS/FAIL */
#define ST_CHECK(name, expr)                                          \
    do {                                                              \
        bool _ok = (expr);                                            \
        ESP_LOGI(ST_TAG, "  [%s] %s", _ok ? "PASS" : "FAIL", name);   \
        if (!_ok) { failed++; }                                       \
        total++;                                                      \
    } while (0)

/* ---- Storage — SD 卡读写测试（VFS，不依赖 FATFS 私有头） ---- */
static bool selftest_storage(void)
{
    const char *path = "/sdcard/selftest.txt";
    const char *magic = "FACE_SELFTEST_OK_2026";
    char buf[64] = {0};

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(ST_TAG, "  storage: fopen(wb) failed, errno=%d", errno);
        return false;
    }
    size_t wlen = fwrite(magic, 1, strlen(magic), f);
    fflush(f);
    fclose(f);
    if (wlen != strlen(magic)) {
        ESP_LOGE(ST_TAG, "  storage: fwrite short %u/%u",
                 (unsigned)wlen, (unsigned)strlen(magic));
        return false;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(ST_TAG, "  storage: fopen(rb) failed, errno=%d", errno);
        return false;
    }
    size_t rlen = fread(buf, 1, strlen(magic), f);
    fclose(f);
    if (rlen != strlen(magic) || strcmp(buf, magic) != 0) {
        ESP_LOGE(ST_TAG, "  storage: readback mismatch");
        return false;
    }
    remove(path);
    return true;
}

/* ---- Display — 显示彩条画面 ---- */
static bool selftest_display(void)
{
    const dal_display_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_display_get("rpi7pin", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->fill == NULL || ops->set_backlight == NULL) {
        return false;
    }
    if (ops->set_backlight(ctx, 80) != DAL_OK) {
        return false;
    }
    /* 画 4 色彩条（红/绿/蓝/白），验证 fill 通路 */
    if (ops->fill(ctx,   0, 0, 200, 480, 0xF800) != DAL_OK) return false;
    if (ops->fill(ctx, 200, 0, 200, 480, 0x07E0) != DAL_OK) return false;
    if (ops->fill(ctx, 400, 0, 200, 480, 0x001F) != DAL_OK) return false;
    if (ops->fill(ctx, 600, 0, 200, 480, 0xFFFF) != DAL_OK) return false;
    selftest_delay_ms(800);
    ops->fill(ctx, 0, 0, 800, 480, 0x0000);   /* 清屏 */
    return true;
}

/* ---- Camera — 设备注册与 ops 完整性校验 ---- */
static bool selftest_camera(void)
{
    const dal_camera_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_camera_get("main_cam", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    /* 阶段3：capture 式接口，校验 capture_frame/return_frame 非 NULL
     * （start_stream/stop_stream 已从 DAL 移除） */
    if (ops->capture_frame == NULL || ops->return_frame == NULL) {
        return false;
    }
    return true;
}

/* ---- Camera — 帧采集轻量测试（capture_frame 取 3 帧验证） ---- */
/* 阶段3：改为 capture 式。BSP 不再创建采集任务，selftest 直接同步调
 * capture_frame 取帧并 return_frame 归还，取 3 帧验证 len>0 即 PASS。 */
static bool selftest_camera_stream(void)
{
    const dal_camera_ops_t *cops = NULL;
    void *cctx = NULL;
    if (dal_camera_get("main_cam", &cops, &cctx) != DAL_OK || cops == NULL) {
        return false;
    }
    if (cops->capture_frame == NULL || cops->return_frame == NULL) {
        return false;
    }

    uint32_t got = 0;
    for (uint32_t i = 0; i < 3; i++) {
        dal_camera_frame_t frame;
        if (cops->capture_frame(cctx, 1000, &frame) != DAL_OK) {
            continue;
        }
        if (frame.buf != NULL && frame.len > 0) {
            got++;
        }
        cops->return_frame(cctx, &frame);
    }
    ESP_LOGI(ST_TAG, "camdisp: capture test, valid frames=%u/3", (unsigned)got);
    return got > 0;
}

/* ---- Touch — 读取并校验坐标范围 ---- */
static bool selftest_touch(void)
{
    const dal_touch_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_touch_get("main_touch", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->read == NULL) {
        return false;
    }
    dal_touch_point_t pts[1];
    uint8_t cnt = 0;
    if (ops->read(ctx, pts, 1, &cnt) != DAL_OK) {
        return false;
    }
    if (cnt > 0) {
        if (pts[0].x >= 800 || pts[0].y >= 480) {
            return false;
        }
    }
    return true;
}

/* ---- Relay — door_lock/alarm 只读；wiegand_pwr 切换验证 ---- */
static bool selftest_relay_readonly(const char *name)
{
    const dal_relay_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_relay_get(name, &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->get == NULL) {
        return false;
    }
    bool state = false;
    return (ops->get(ctx, &state) == DAL_OK);
}

static bool selftest_relay_toggle(const char *name)
{
    const dal_relay_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_relay_get(name, &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->set == NULL) {
        return false;
    }
    if (ops->set(ctx, true) != DAL_OK) {
        ESP_LOGE(ST_TAG, "  relay %s: set(ON) failed", name);
        return false;
    }
    selftest_delay_ms(20);
    if (ops->set(ctx, false) != DAL_OK) {
        ESP_LOGE(ST_TAG, "  relay %s: set(OFF) failed", name);
        return false;
    }
    return true;
}

/* ---- Audio — 播放 1kHz 方波提示音 ---- */
#define ST_AUDIO_SAMPLES 4800u   /* 0.3s @ 16kHz */
static int16_t s_audio_buf[ST_AUDIO_SAMPLES * 2u];

static bool selftest_audio(void)
{
    const dal_audio_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_audio_get("main_audio", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->play == NULL) {
        return false;
    }
    const uint32_t N = ST_AUDIO_SAMPLES;
    for (uint32_t i = 0; i < N; i++) {
        int16_t s = ((i % 16u) < 8u) ? 6000 : -6000;
        if (i < 80) s = (int16_t)((int32_t)s * (int32_t)i / 80);
        else if (i > N - 80) s = (int16_t)((int32_t)s * (int32_t)(N - i) / 80);
        s_audio_buf[i * 2u]     = s;
        s_audio_buf[i * 2u + 1] = s;
    }
    if (ops->set_volume) ops->set_volume(ctx, 60);
    if (ops->set_mute)   ops->set_mute(ctx, false);
    if (ops->play(ctx, s_audio_buf, sizeof(s_audio_buf)) != DAL_OK) {
        return false;
    }
    /* 写静音冲刷 DMA，再 mute 彻底停声 */
    static int16_t silence[ST_AUDIO_SAMPLES * 2u];
    memset(silence, 0, sizeof(silence));
    ops->play(ctx, silence, sizeof(silence));
    if (ops->set_mute) ops->set_mute(ctx, true);
    return true;
}

/* ---- Network — 查询连接状态 ---- */
static bool selftest_network(void)
{
    const dal_network_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_network_get("main_eth", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->is_connected == NULL) {
        return false;
    }
    (void)ops->is_connected(ctx);
    return true;
}

/* ---- PIR ---- */
static bool selftest_pir(void)
{
    const dal_pir_ops_t *ops = NULL;
    void *ctx = NULL;
    if (dal_pir_get("main_pir", &ops, &ctx) != DAL_OK || ops == NULL) {
        return false;
    }
    if (ops->get_state == NULL) {
        return false;
    }
    dal_pir_state_t st = DAL_PIR_STATE_IDLE;
    return (ops->get_state(ctx, &st) == DAL_OK);
}

/** 运行全部设备测试，返回失败数 */
static uint16_t selftest_run(void)
{
    uint16_t total = 0;
    uint16_t failed = 0;

    ESP_LOGI(ST_TAG, "==== board selftest start ====");
    ST_CHECK("storage:SD write+read+verify", selftest_storage());
    ST_CHECK("display:fill color bars", selftest_display());
    ST_CHECK("camera:registered+ops", selftest_camera());
    ST_CHECK("camera:capture 3 frames test", selftest_camera_stream());
    ST_CHECK("touch:read", selftest_touch());
    ST_CHECK("relay:door_lock:get(read-only)", selftest_relay_readonly("door_lock"));
    ST_CHECK("relay:alarm:get(read-only)", selftest_relay_readonly("alarm"));
    ST_CHECK("relay:wiegand_pwr:set toggle", selftest_relay_toggle("wiegand_pwr"));
    ST_CHECK("audio:play 1kHz tone", selftest_audio());
    ST_CHECK("network:is_connected", selftest_network());
    ST_CHECK("pir:get_state", selftest_pir());

    ESP_LOGI(ST_TAG, "==== selftest done: %u/%u passed, %u failed ====",
             total - failed, total, failed);
    return failed;
}

/* ================================================================
 *  Assembler
 * ================================================================ */

void app_main(void)
{
    dal_err_t ret;
    /* ====== Step 0: NVS + 网络事件循环 ====== */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_init();

    /* 时区：中国标准时间 UTC+8 */
    setenv("TZ", "CST-8", 1);
    tzset();

    /* SNTP 无条件启动（后台轮询 pool.ntp.org，无需等 MQTT 连接） */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    /* ====== Step 1: BSP 组装（共享 I2C + create + register，零硬件副作用） ====== */
    if (board_v1_init() != DAL_OK) {
        ESP_LOGE(ASM_TAG, "BSP assemble failed");
        return;
    }

    /* ----- 中间件初始化 ----- */
    if (DAL_OK != msg_queue_init()) {
        ESP_LOGE(ASM_TAG, "msg queue assemble failed");
        return;
    }


    /* ====== Step 2: 从 DAL 取 ops+ctx ====== */
    const dal_camera_ops_t  *cam_ops    = NULL; void *cam_ctx    = NULL;
    const dal_display_ops_t *disp_ops   = NULL; void *disp_ctx   = NULL;
    const dal_touch_ops_t   *touch_ops  = NULL; void *touch_ctx  = NULL;
    const dal_audio_ops_t   *audio_ops  = NULL; void *audio_ctx  = NULL;
    const dal_network_ops_t *net_ops    = NULL; void *net_ctx    = NULL;
    const dal_pir_ops_t     *pir_ops    = NULL; void *pir_ctx    = NULL;
    const dal_relay_ops_t   *lock_ops   = NULL; void *lock_ctx   = NULL;
    const dal_relay_ops_t   *alarm_ops  = NULL; void *alarm_ctx  = NULL;
    const dal_relay_ops_t   *wieg_ops   = NULL; void *wieg_ctx   = NULL;

    dal_camera_get("main_cam",   &cam_ops,   &cam_ctx);
    dal_display_get("rpi7pin",   &disp_ops,  &disp_ctx);
    dal_touch_get("main_touch",  &touch_ops, &touch_ctx);
    dal_audio_get("main_audio",  &audio_ops, &audio_ctx);
    dal_network_get("main_eth",  &net_ops,   &net_ctx);
    dal_pir_get("main_pir",      &pir_ops,   &pir_ctx);
    dal_relay_get("door_lock",   &lock_ops,  &lock_ctx);
    dal_relay_get("alarm",       &alarm_ops, &alarm_ctx);
    dal_relay_get("wiegand_pwr", &wieg_ops,  &wieg_ctx);


    /* ====== Step 2.5: 预加载 AI 引擎（先加载两模型再启 service，避免 dl_init 期间
     *    detect 任务并发推理踩 ESP-DL 内部堆） ====== */
    face_engine_install_dl();

    /* ====== Step 3: Service 注入 + 启动（各 service _start() 同步初始化所需硬件） ======
     *
     * 原则：硬件初始化由各服务层负责，Assembler 不做硬件 init。
     * 每个 service 的 _start() 先同步 init 硬件，再创建 worker 任务。
     */

    /* ----- camera（_start 同步 init camera + PIR，再创建 worker） ----- */
    const svc_camera_deps_t cam_deps = {
      .cam_ops = (dal_camera_ops_t *)cam_ops,
      .cam_ctx = cam_ctx,
      .pir_ops = (dal_pir_ops_t *)pir_ops,
      .pir_ctx = pir_ctx,
    };
    ret = svc_camera_init(&cam_deps);
    if (DAL_OK == ret) {
        svc_camera_start();
    }

    /* ----- touch（_start 同步 init 触摸，再创建 worker） ----- */
    const svc_touch_deps_t touch_deps = {
      .ops = (dal_touch_ops_t *)touch_ops,
      .ctx = touch_ctx,
    };
    ret = svc_touch_init(&touch_deps);
    if (DAL_OK == ret) {
        svc_touch_start();
    }

    /* ----- face_detect（消费 camera 共享帧，输出人脸框到 UI） ----- */
    ret = svc_face_detect_init();
    if (DAL_OK == ret) {
        svc_face_detect_start();
    }
    /* ----- face_feature（收裁剪人脸，懒加载提取特征向量） ----- */
    ret = svc_face_feature_init();
    if (DAL_OK == ret) {
        svc_face_feature_start();
    }

    /* ----- UI（_start 同步 init 显示，再创建 LVGL 任务） ----- */
    const svc_ui_deps_t ui_deps = {
      .disp_ops = (dal_display_ops_t *)disp_ops,
      .disp_ctx = disp_ctx,
    };
    ret = svc_ui_init(&ui_deps);
    if (DAL_OK == ret) {
        svc_ui_start();
    }

    /* ----- 以下设备暂缺专用 service，在 Assembler 临时集中 init -----
     * TODO: 迁入各自 service（svc_audio / svc_mqtt net init / svc_perm_manager relay init） */
    if (audio_ops && audio_ops->init) {
        dal_audio_config_t ac = {.sample_rate_hz=BOARD_AUDIO_SAMPLE_RATE,.volume=60};
        audio_ops->init(audio_ctx, &ac);
    }
    if (net_ops && net_ops->init) {
        dal_network_config_t nc = {.use_dhcp=BOARD_NETWORK_USE_DHCP};
        net_ops->init(net_ctx, &nc, NULL, NULL);
    }
    if (lock_ops && lock_ops->init)  lock_ops->init(lock_ctx);
    if (alarm_ops && alarm_ops->init) alarm_ops->init(alarm_ctx);
    if (wieg_ops && wieg_ops->init)  wieg_ops->init(wieg_ctx);

    /* ----- perm_manager（依赖 relay，硬件已 init） ----- */
    svc_perm_manager_deps_t perm_deps = {
        .door_lock_ops=lock_ops,.door_lock_ctx=lock_ctx,
        .alarm_ops=alarm_ops,.alarm_ctx=alarm_ctx,
    };
    /* perm_manager 在 db_store + face_engine 之后启动（依赖两者） */

    /* ====== Step 5: Middleware 初始化 ====== */
    mw_event_bus_init();
    db_store_init();

    /* perm_manager 启动（依赖 db_store + face_engine） */
    svc_perm_manager_init(&perm_deps);

    /* ====== OTA 回滚确认（新固件启动后标记有效） ====== */
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(ASM_TAG,"OTA: mark app valid, rollback cancelled");
        }
    }

    /* ====== Step 6: 网络服务（mqtt / ota） ====== */
    svc_mqtt_deps_t mqtt_deps = {.network_ops=net_ops,.network_ctx=net_ctx};
    if (svc_mqtt_init(&mqtt_deps) == DAL_OK) svc_mqtt_start();

    svc_ota_init(NULL);

    /* ====== Step 7: 尾循环：周期性刷 DB 到 SD 卡 ====== */
    uint32_t flush_tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        flush_tick++;
        if (flush_tick >= 30) {  /* 每 30 秒尝试 flush */
            flush_tick = 0;
            db_store_flush();
        }
    }
}
