/**
 * @file    bsp_touch_ft5406.c
 * @brief   FT5406 触摸屏 BSP 实现 — I2C 原生 + 轮询模型
 *
 * @details 直接调用 ESP-IDF driver/i2c_master 读写 FT5406EE8 触摸控制器，
 *          实现 dal_touch_ops_t 契约。bsp_touch_ft5406_create() 仅绑定
 *          ops + ctx，不注册 DAL、不驱动硬件；硬件初始化（共享 I2C 总线
 *          挂载 FT5406 设备 + 写寄存器序列）封装在 ops->init()，由上层
 *          按需触发。esp_err_t 在 ops 边界翻译为 dal_err_t，不透传。
 *
 *          - init：从 board 获取共享 I2C 总线，挂载设备，写初始化寄存器序列。
 *          - read：读 0x02 点数 → 批量读各点 6 字节 → 解析坐标（含 X/Y 取反
 *            校准），输出多点。纯轮询，无中断。
 *          - deinit：从总线移除 I2C 设备。
 *          FT5406EE8 寄存器布局（每点 6 字节，从 0x03 起）：
 *            p[0]: [7:6]=event_id, [3:0]=x_hi
 *            p[1]: x_lo
 *            p[2]: [7:4]=touch_id, [3:0]=y_hi
 *            p[3]: y_lo
 *
 * @author  xLumina
 * @version 1.1
 */
#include "bsp_touch_ft5406.h"
#include "dal_touch_interface.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"
#include "board_v1.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ---- FT5406EE8 寄存器 ---- */
#define FT5406_REG_TD_STATUS      0x02   /**< 触控点数 */
#define FT5406_REG_TOUCH1_XH      0x03   /**< 触控点1起始 */
#define FT5406_REG_CHIP_ID        0xA8   /**< 芯片 ID */
#define FT5406_REG_DEVICE_MODE    0x00   /**< 工作模式 */
#define FT5406_REG_TH_GROUP       0x80   /**< 触发阈值 */
#define FT5406_REG_PERIOD_ACTIVE  0x88   /**< 活跃周期 */

#define FT5406_TOUCH_POINT_SIZE   6      /**< 每点 6 字节 */
#define FT5406_MAX_POINTS         5      /**< 最大支持 5 点 */

/* ---- BSP 私有上下文 ---- */
typedef struct {
    i2c_master_dev_handle_t dev;   /**< I2C 设备句柄（共享总线挂载） */
    uint16_t                h_res;
    uint16_t                v_res;
    bool                    inited;
} bsp_ft5406_ctx_t;

static bsp_ft5406_ctx_t s_ctx;

/* ================================================================
 *  I2C 寄存器读写（内联等价原 PAL 封装）
 * ================================================================ */

/** @brief 读寄存器：transmit 寄存器地址后 receive 数据（restart 读） */
static dal_err_t ft5406_read_reg(i2c_master_dev_handle_t dev, uint8_t reg,
                                 uint8_t *out, size_t len)
{
    return dal_err_from_esp(i2c_master_transmit_receive(dev, &reg, 1, out, len, -1));
}

/** @brief 写单字节寄存器：transmit [reg, val] 两字节 */
static dal_err_t ft5406_write_reg_byte(i2c_master_dev_handle_t dev, uint8_t reg,
                                       uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return dal_err_from_esp(i2c_master_transmit(dev, buf, sizeof(buf), -1));
}

/* ================================================================
 *  dal_touch_ops_t 实现
 * ================================================================ */
static dal_err_t ft5406_init(void *ctx_, const dal_touch_config_t *cfg)
{
    bsp_ft5406_ctx_t *ctx = (bsp_ft5406_ctx_t *)ctx_;
    if (ctx == NULL || cfg == NULL) {
        return DAL_ERR_INVALID;
    }
    if (ctx->inited) {
        return DAL_ERR_STATE;
    }

    ctx->h_res = cfg->h_res;
    ctx->v_res = cfg->v_res;

    /* 从 board 获取共享 I2C 总线并挂载 FT5406 设备 */
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)board_i2c_get_bus();
    if (bus == NULL) {
        ESP_LOGE("FT5406", "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BOARD_TOUCH_FT5406_I2C_ADDR,
        .scl_speed_hz    = BOARD_I2C_FREQ_HZ,
        .scl_wait_us     = 0,   /* 使用驱动默认值 */
        .flags = {
            .disable_ack_check = false,
        },
    };
    esp_err_t e = i2c_master_bus_add_device(bus, &dev_cfg, &ctx->dev);
    if (e != ESP_OK) {
        return dal_err_from_esp(e);
    }

    /* 读芯片 ID 校验（失败仅告警） */
    uint8_t chip_id = 0;
    if (ft5406_read_reg(ctx->dev, FT5406_REG_CHIP_ID, &chip_id, 1) == DAL_OK) {
        ESP_LOGI("FT5406", "chip id: 0x%02X", chip_id);
    } else {
        ESP_LOGW("FT5406", "read chip id failed, continue");
    }

    /* 唤醒：DEVICE_MODE = normal(0x00) */
    dal_err_t ret = ft5406_write_reg_byte(ctx->dev, FT5406_REG_DEVICE_MODE, 0x00);
    if (ret != DAL_OK) {
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
        return ret;
    }
    /* 触发阈值（默认 0x16） */
    ft5406_write_reg_byte(ctx->dev, FT5406_REG_TH_GROUP, 0x16);
    /* 活跃周期（0x0E） */
    ft5406_write_reg_byte(ctx->dev, FT5406_REG_PERIOD_ACTIVE, 0x0E);
    vTaskDelay(pdMS_TO_TICKS(20));

    ctx->inited = true;
    ESP_LOGI("FT5406", "initialized (%ux%u)", ctx->h_res, ctx->v_res);
    return DAL_OK;
}

static dal_err_t ft5406_read(void *ctx_, dal_touch_point_t *points,
                             uint8_t max_count, uint8_t *out_count)
{
    bsp_ft5406_ctx_t *ctx = (bsp_ft5406_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited || points == NULL || out_count == NULL) {
        return DAL_ERR_INVALID;
    }
    *out_count = 0;
    if (max_count == 0) {
        return DAL_OK;
    }

    /* 读触控点数 */
    uint8_t status = 0;
    dal_err_t ret = ft5406_read_reg(ctx->dev, FT5406_REG_TD_STATUS, &status, 1);
    if (ret != DAL_OK) {
        return ret;
    }
    uint8_t num = status & 0x0F;
    if (num == 0) {
        return DAL_OK;   /* 无触摸 */
    }
    if (num > FT5406_MAX_POINTS) {
        num = FT5406_MAX_POINTS;
    }
    if (num > max_count) {
        num = max_count;
    }

    /* 批量读取所有触控点数据：写起始寄存器后连续读 num*6 字节
     * （FT5406 寄存器自增，transmit_receive 单次事务完成） */
    uint8_t buf[FT5406_MAX_POINTS * FT5406_TOUCH_POINT_SIZE] = {0};
    uint8_t reg = FT5406_REG_TOUCH1_XH;
    esp_err_t e = i2c_master_transmit_receive(ctx->dev, &reg, 1,
                                              buf, num * FT5406_TOUCH_POINT_SIZE, -1);
    if (e != ESP_OK) {
        ESP_LOGE("FT5406", "read touch data failed: %d", e);
        return dal_err_from_esp(e);
    }

    /* 解析触控点（X/Y 取反校准：FT5406 坐标系与 LCD 相反） */
    for (uint8_t i = 0; i < num; i++) {
        uint8_t *p = buf + i * FT5406_TOUCH_POINT_SIZE;
        uint8_t  event_id = p[0] >> 6;
        uint16_t x = ((uint16_t)(p[0] & 0x0F) << 8) | p[1];
        uint8_t  touch_id = p[2] >> 4;
        uint16_t y = ((uint16_t)(p[2] & 0x0F) << 8) | p[3];

        uint16_t rx = (uint16_t)(ctx->h_res - 1 - x);
        uint16_t ry = (uint16_t)(ctx->v_res - 1 - y);
        if (rx >= ctx->h_res || ry >= ctx->v_res) {
            continue;   /* 坐标越界，丢弃 */
        }

        points[*out_count].id    = touch_id;
        points[*out_count].x     = rx;
        points[*out_count].y     = ry;
        /* FT5406 event_id（p[0]>>6）：0=Down, 1=Up, 2=Contact, 3=No event。
         * 不能直接强转 dal_touch_event_t（其编码 0=DOWN,1=MOVE,2=UP，与
         * FT5406 不同），否则 Contact(2) 误报为 UP、Up(1) 误报为 MOVE。 */
        switch (event_id) {
        case 0:  points[*out_count].event = DAL_TOUCH_EVENT_DOWN; break;
        case 1:  points[*out_count].event = DAL_TOUCH_EVENT_UP;   break;
        case 2:  points[*out_count].event = DAL_TOUCH_EVENT_MOVE; break;
        default: points[*out_count].event = DAL_TOUCH_EVENT_MOVE; break;
        }
        (*out_count)++;
    }

    return DAL_OK;
}

static dal_err_t ft5406_deinit(void *ctx_)
{
    bsp_ft5406_ctx_t *ctx = (bsp_ft5406_ctx_t *)ctx_;
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }
    if (ctx->dev != NULL) {
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
    }
    ctx->inited = false;
    return DAL_OK;
}

/* ================================================================
 *  静态 ops + ctx（单实例，ctx 编译期注入 ops->ctx）
 * ================================================================ */
static dal_touch_ops_t s_ft5406_ops = {
    .init   = ft5406_init,
    .read   = ft5406_read,
    .deinit = ft5406_deinit,
    .ctx    = &s_ctx,
};

/* ================================================================
 *  对外 create 入口（仅绑定，不注册、不初始化硬件）
 * ================================================================ */
dal_touch_ops_t *bsp_touch_ft5406_create(void)
{
    /* 单实例：ctx 已编译期注入 ops->ctx */
    memset(&s_ctx, 0, sizeof(s_ctx));

    return &s_ft5406_ops;
}
