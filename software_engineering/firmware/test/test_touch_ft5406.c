/**
 * @file    test_touch_ft5406.c
 * @brief   FT5406 触摸屏 BSP 单元测试（宿主机 Mock 测试）
 *
 * @details 验证 FT5406EE6 触摸点解析协议：
 *            - 读 0x02 获取点数
 *            - 批量读 0x03 起每点 6 字节
 *            - 解析 event_id/x/y/touch_id，并做 X/Y 取反校准
 *          pal_i2c_read_reg/read 用 custom fake 返回构造数据。
 *
 *  每点 6 字节布局：
 *    p[0]: [7:6]=event_id, [3:0]=x_hi
 *    p[1]: x_lo
 *    p[2]: [7:4]=touch_id, [3:0]=y_hi
 *    p[3]: y_lo
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"
#include "board_v1.h"

/* ---- 被测模块 ---- */
#include "bsp_touch_ft5406.h"
#include "dal_touch.h"
#include "dal_touch_interface.h"
#include "dal_err.h"
#include "bsp_config.h"
#include <string.h>

/* ================================================================
 *  custom fake 控制 I2C 读返回
 * ================================================================ */
static uint8_t s_td_status;        /* 0x02 寄存器返回的触控点数 */
static uint8_t s_touch_buf[64];    /* 批量读返回的触摸点数据 */
static size_t  s_touch_buf_len;

static int attach_sets_dev(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                           const pal_i2c_dev_config_t *cfg)
{
    (void)bus; (void)cfg;
    if (dev) *dev = (pal_i2c_dev_handle_t)1;
    return 0;
}

static int read_reg_fake(pal_i2c_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    (void)dev; (void)len;
    if (data == NULL) return 0;
    if (reg == 0x02) {
        data[0] = s_td_status;        /* 触控点数 */
    } else if (reg == 0xA8) {
        data[0] = 0x64;               /* chip id 占位 */
    } else {
        data[0] = 0;
    }
    return 0;
}

static int read_fake(pal_i2c_dev_handle_t dev, uint8_t *data, size_t len)
{
    (void)dev;
    size_t n = (len < s_touch_buf_len) ? len : s_touch_buf_len;
    if (data) memcpy(data, s_touch_buf, n);
    return 0;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bool s_inited = false;
static const dal_touch_ops_t *s_ops = NULL;
static void                  *s_ctx = NULL;

void setUp(void)
{
    RESET_FAKE(pal_i2c_dev_attach);
    RESET_FAKE(pal_i2c_dev_detach);
    RESET_FAKE(pal_i2c_write);
    RESET_FAKE(pal_i2c_read);
    RESET_FAKE(pal_i2c_read_reg);
    RESET_FAKE(pal_i2c_write_reg_byte);

    pal_i2c_dev_attach_fake.return_val    = 0;
    pal_i2c_dev_attach_fake.custom_fake   = attach_sets_dev;
    pal_i2c_dev_detach_fake.return_val    = 0;
    pal_i2c_write_fake.return_val         = 0;
    pal_i2c_write_reg_byte_fake.return_val= 0;
    pal_i2c_read_reg_fake.custom_fake     = read_reg_fake;
    pal_i2c_read_fake.custom_fake         = read_fake;

    s_td_status = 0;
    memset(s_touch_buf, 0, sizeof(s_touch_buf));
    s_touch_buf_len = 0;

    /* init 仅一次（dal_touch 注册表全局不可重置） */
    if (!s_inited) {
        TEST_ASSERT_EQUAL(DAL_OK, bsp_touch_ft5406_init());
        TEST_ASSERT_EQUAL(DAL_OK, dal_touch_get("main_touch", &s_ops, &s_ctx));
        s_inited = true;
    }
}

void tearDown(void) {}

/* ---- 构造一个触摸点的 6 字节 ---- */
static void build_point(uint8_t *p, uint8_t event, uint16_t x, uint16_t y, uint8_t id)
{
    p[0] = (uint8_t)((event << 6) | ((x >> 8) & 0x0F));
    p[1] = (uint8_t)(x & 0xFF);
    p[2] = (uint8_t)((id << 4) | ((y >> 8) & 0x0F));
    p[3] = (uint8_t)(y & 0xFF);
    p[4] = 0;
    p[5] = 0;
}

/* ================================================================
 *  无触摸
 * ================================================================ */
void test_touch_read_no_touch_returns_zero_points(void)
{
    s_td_status = 0;
    dal_touch_point_t pts[2];
    uint8_t count = 99;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 2, &count));
    TEST_ASSERT_EQUAL(0, count);
}

/* ================================================================
 *  单点解析 + X/Y 取反
 * ================================================================ */
void test_touch_read_single_point_down_event(void)
{
    s_td_status = 1;
    /* 原始 x=100, y=200, event=DOWN(0), id=1 */
    build_point(s_touch_buf, 0, 100, 200, 1);
    s_touch_buf_len = 6;

    dal_touch_point_t pts[2];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 2, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(DAL_TOUCH_EVENT_DOWN, pts[0].event);
    TEST_ASSERT_EQUAL(1, pts[0].id);
    /* X/Y 取反：rx = h_res-1-x = 800-1-100 = 699 */
    TEST_ASSERT_EQUAL(BOARD_TOUCH_H_RES - 1 - 100, pts[0].x);
    TEST_ASSERT_EQUAL(BOARD_TOUCH_V_RES - 1 - 200, pts[0].y);
}

void test_touch_read_single_point_move_event(void)
{
    s_td_status = 1;
    build_point(s_touch_buf, 1, 0, 0, 3);   /* event=MOVE(1) */
    s_touch_buf_len = 6;

    dal_touch_point_t pts[2];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 2, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(DAL_TOUCH_EVENT_MOVE, pts[0].event);
    TEST_ASSERT_EQUAL(3, pts[0].id);
    /* 角点：x=0,y=0 → rx=h_res-1, ry=v_res-1 */
    TEST_ASSERT_EQUAL(BOARD_TOUCH_H_RES - 1, pts[0].x);
    TEST_ASSERT_EQUAL(BOARD_TOUCH_V_RES - 1, pts[0].y);
}

void test_touch_read_single_point_up_event(void)
{
    s_td_status = 1;
    build_point(s_touch_buf, 2, 799, 479, 0);  /* event=UP(2), 另一角 */
    s_touch_buf_len = 6;

    dal_touch_point_t pts[2];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 2, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(DAL_TOUCH_EVENT_UP, pts[0].event);
    /* x=799 → rx=0；y=479 → ry=0 */
    TEST_ASSERT_EQUAL(0, pts[0].x);
    TEST_ASSERT_EQUAL(0, pts[0].y);
}

/* ================================================================
 *  多点
 * ================================================================ */
void test_touch_read_two_points(void)
{
    s_td_status = 2;
    build_point(s_touch_buf,      0, 100, 100, 1);
    build_point(s_touch_buf + 6,  1, 200, 200, 2);
    s_touch_buf_len = 12;

    dal_touch_point_t pts[5];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 5, &count));
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(1, pts[0].id);
    TEST_ASSERT_EQUAL(2, pts[1].id);
}

/* ================================================================
 *  点数超过 max_count 截断
 * ================================================================ */
void test_touch_read_truncates_to_max_count(void)
{
    s_td_status = 3;
    build_point(s_touch_buf,       0, 10, 10, 1);
    build_point(s_touch_buf + 6,   0, 20, 20, 2);
    build_point(s_touch_buf + 12,  0, 30, 30, 3);
    s_touch_buf_len = 18;

    dal_touch_point_t pts[2];   /* 仅容纳 2 */
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->read(s_ctx, pts, 2, &count));
    TEST_ASSERT_EQUAL(2, count);
}

/* ================================================================
 *  参数校验
 * ================================================================ */
void test_touch_read_null_args_should_fail(void)
{
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, s_ops->read(s_ctx, NULL, 2, &count));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, s_ops->read(s_ctx, (dal_touch_point_t[1]){0}, 2, NULL));
}

/* ================================================================
 *  I2C 读取失败翻译
 * ================================================================ */
void test_touch_read_reg_fail_returns_hw_error(void)
{
    s_td_status = 1;
    pal_i2c_read_reg_fake.custom_fake = NULL;
    pal_i2c_read_reg_fake.return_val  = ESP_FAIL;

    dal_touch_point_t pts[2];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(DAL_ERR_HW, s_ops->read(s_ctx, pts, 2, &count));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_touch_read_no_touch_returns_zero_points);
    RUN_TEST(test_touch_read_single_point_down_event);
    RUN_TEST(test_touch_read_single_point_move_event);
    RUN_TEST(test_touch_read_single_point_up_event);
    RUN_TEST(test_touch_read_two_points);
    RUN_TEST(test_touch_read_truncates_to_max_count);
    RUN_TEST(test_touch_read_null_args_should_fail);
    RUN_TEST(test_touch_read_reg_fail_returns_hw_error);

    return UNITY_END();
}
