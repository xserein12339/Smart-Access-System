/**
 * @file    bsp_touch_ft5406.c
 * @brief   FT5406 触摸屏 BSP 实现 — 轮询模型
 *
 * @details 基于 PAL i2c 读写 FT5406EE8 触摸控制器，实现 dal_touch_ops_t：
 *          - init：从 board 获取共享 I2C 总线，挂载设备，写初始化寄存器序列。
 *          - read：读 0x02 点数 → 批量读各点 6 字节 → 解析坐标（含 X/Y 取反
 *            校准），输出多点。纯轮询，无中断。
 *          - deinit：detach I2C 设备。
 *          FT5406EE8 寄存器布局（每点 6 字节，从 0x03 起）：
 *            p[0]: [7:6]=event_id, [3:0]=x_hi
 *            p[1]: x_lo
 *            p[2]: [7:4]=touch_id, [3:0]=y_hi
 *            p[3]: y_lo
 *          PAL 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_touch_ft5406.h"
#include "dal_touch.h"
#include "dal_touch_interface.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_i2c.h"
#include "board_v1.h"
#include "pal_log.h"
#include "osal_task.h"
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
    pal_i2c_dev_handle_t dev;     /**< PAL I2C 设备句柄 */
    uint16_t             h_res;
    uint16_t             v_res;
    bool                 inited;
} bsp_ft5406_ctx_t;

static bsp_ft5406_ctx_t s_ctx;

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

    /* 读芯片 ID 校验（失败仅告警） */
    uint8_t chip_id = 0;
    if (pal_i2c_read_reg(ctx->dev, FT5406_REG_CHIP_ID, &chip_id, 1) == 0) {
        PAL_LOGI("FT5406", "chip id: 0x%02X", chip_id);
    } else {
        PAL_LOGW("FT5406", "read chip id failed, continue");
    }

    /* 唤醒：DEVICE_MODE = normal(0x00) */
    int ret = pal_i2c_write_reg_byte(ctx->dev, FT5406_REG_DEVICE_MODE, 0x00);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    /* 触发阈值（默认 0x16） */
    pal_i2c_write_reg_byte(ctx->dev, FT5406_REG_TH_GROUP, 0x16);
    /* 活跃周期（0x0E） */
    pal_i2c_write_reg_byte(ctx->dev, FT5406_REG_PERIOD_ACTIVE, 0x0E);
    osal_task_delay_ms(20);

    ctx->inited = true;
    PAL_LOGI("FT5406", "initialized (%ux%u)", ctx->h_res, ctx->v_res);
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
    int ret = pal_i2c_read_reg(ctx->dev, FT5406_REG_TD_STATUS, &status, 1);
    if (ret != 0) {
        return dal_err_from_pal(ret);
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

    /* 批量读取所有触控点数据：写起始寄存器，再连续读 num*6 字节 */
    uint8_t buf[FT5406_MAX_POINTS * FT5406_TOUCH_POINT_SIZE] = {0};
    uint8_t reg = FT5406_REG_TOUCH1_XH;
    ret = pal_i2c_write(ctx->dev, &reg, 1);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    ret = pal_i2c_read(ctx->dev, buf, num * FT5406_TOUCH_POINT_SIZE);
    if (ret != 0) {
        PAL_LOGE("FT5406", "read touch data failed: %d", ret);
        return dal_err_from_pal(ret);
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
        points[*out_count].event = (dal_touch_event_t)event_id;
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
        pal_i2c_dev_detach(ctx->dev);
        ctx->dev = NULL;
    }
    ctx->inited = false;
    return DAL_OK;
}

static const dal_touch_ops_t s_ft5406_ops = {
    .init   = ft5406_init,
    .read   = ft5406_read,
    .deinit = ft5406_deinit,
};

/* ================================================================
 *  对外初始化入口（自注册）
 * ================================================================ */
dal_err_t bsp_touch_ft5406_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    /* 从 board 获取共享 I2C 总线 */
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)board_i2c_get_bus();
    if (bus == NULL) {
        PAL_LOGE("FT5406", "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }

    /* 挂载 FT5406 I2C 设备 */
    pal_i2c_dev_config_t dev_cfg = {
        .device_address    = BOARD_TOUCH_FT5406_I2C_ADDR,
        .scl_speed_hz      = BOARD_I2C_FREQ_HZ,
        .disable_ack_check = false,
    };
    int ret = pal_i2c_dev_attach(&s_ctx.dev, bus, &dev_cfg);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }

    /* 用板级默认分辨率初始化 */
    dal_touch_config_t cfg = {
        .h_res = BOARD_TOUCH_H_RES,
        .v_res = BOARD_TOUCH_V_RES,
    };
    dal_err_t dret = ft5406_init(&s_ctx, &cfg);
    if (dret != DAL_OK) {
        pal_i2c_dev_detach(s_ctx.dev);
        s_ctx.dev = NULL;
        return dret;
    }

    /* 自注册到 DAL */
    return dal_touch_register("main_touch", &s_ft5406_ops, &s_ctx);
}
