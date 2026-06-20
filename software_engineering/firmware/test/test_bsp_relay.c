/**
 * @file    test_bsp_relay.c
 * @brief   bsp_relay ops + 错误码翻译单元测试（宿主机 Mock 测试）
 *
 * @details 测试 bsp_relay 的 GPIO ops 实现（set/get）与 PAL 错误码到
 *          dal_err_t 的边界翻译。pal_gpio 用 FFF fake 拦截。
 *
 *          bsp_relay_init() 内部调 dal_relay_register 注册 3 个继电器，
 *          dal_relay 注册表为静态全局、无 reset API，故 init 在整个
 *          进程只调用一次（由 setUp 的 s_inited 标志保证）。ops 测试
 *          通过 dal_relay_get 取已注册 ops 调用。
 *
 *  引脚配置（bsp_relay.c 硬编码）：
 *    door_lock   : pin=1, active_high=false（低电平吸合）
 *    alarm       : pin=2, active_high=false
 *    wiegand_pwr : pin=3, active_high=true （高电平吸合）
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头（必须在真实头之前）---- */
#include "esp_err.h"
#include "pal_gpio.h"

/* ---- 被测模块 ---- */
#include "bsp_relay.h"
#include "dal_relay.h"
#include "dal_relay_interface.h"
#include "dal_err.h"

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bool s_inited = false;

void setUp(void)
{
    RESET_FAKE(pal_gpio_set_direction);
    RESET_FAKE(pal_gpio_write);
    RESET_FAKE(pal_gpio_read);

    /* 默认 PAL 调用成功 */
    pal_gpio_set_direction_fake.return_val = 0;
    pal_gpio_write_fake.return_val = 0;
    pal_gpio_read_fake.return_val = 0;

    /* init 仅执行一次：注册 door_lock/alarm/wiegand_pwr 到 dal_relay */
    if (!s_inited) {
        TEST_ASSERT_EQUAL(DAL_OK, bsp_relay_init());
        s_inited = true;
    }
}

void tearDown(void) {}

/* ================================================================
 *  init 行为
 * ================================================================ */
void test_bsp_relay_init_registers_three_relays(void)
{
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;

    /* init 已在 setUp 执行，三个继电器应可查询 */
    TEST_ASSERT_EQUAL(DAL_OK, dal_relay_get("door_lock", &ops, &ctx));
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ctx);

    TEST_ASSERT_EQUAL(DAL_OK, dal_relay_get("alarm", &ops, &ctx));
    TEST_ASSERT_EQUAL(DAL_OK, dal_relay_get("wiegand_pwr", &ops, &ctx));

    /* init 配置 3 个 GPIO 输出 + 各写一次默认电平 */
    TEST_ASSERT_EQUAL(3, pal_gpio_set_direction_fake.call_count);
    TEST_ASSERT_EQUAL(3, pal_gpio_write_fake.call_count);
}

void test_bsp_relay_set_pal_invalid_arg_translates_to_dal_invalid(void)
{
    /* ESP_ERR_INVALID_ARG 应翻译为 DAL_ERR_INVALID（语义对应） */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_write_fake.return_val = ESP_ERR_INVALID_ARG;
    dal_err_t ret = DAL_RELAY_SET(ops, ctx, true);
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, ret);
}

/* ================================================================
 *  set 极性翻译
 * ================================================================ */
void test_relay_set_active_low_on_writes_low_level(void)
{
    /* door_lock: active_high=false，set(true=吸合) 应写低电平 0 */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_write_fake.return_val = 0;
    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_SET(ops, ctx, true));
    TEST_ASSERT_EQUAL(1, pal_gpio_write_fake.call_count);
    TEST_ASSERT_EQUAL(1, pal_gpio_write_fake.arg0_val);  /* pin=1 */
    TEST_ASSERT_EQUAL(0, pal_gpio_write_fake.arg1_val);  /* level=0 */
}

void test_relay_set_active_low_off_writes_high_level(void)
{
    /* door_lock: active_high=false，set(false=断开) 应写高电平 1 */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_SET(ops, ctx, false));
    TEST_ASSERT_EQUAL(1, pal_gpio_write_fake.arg1_val);  /* level=1 */
}

void test_relay_set_active_high_on_writes_high_level(void)
{
    /* wiegand_pwr: active_high=true，set(true) 应写高电平 1 */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("wiegand_pwr", &ops, &ctx);

    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_SET(ops, ctx, true));
    TEST_ASSERT_EQUAL(3, pal_gpio_write_fake.arg0_val);  /* pin=3 */
    TEST_ASSERT_EQUAL(1, pal_gpio_write_fake.arg1_val);  /* level=1 */
}

/* ================================================================
 *  get 极性翻译
 * ================================================================ */
void test_relay_get_active_low_reads_level_zero_as_on(void)
{
    /* door_lock: active_high=false，GPIO 读 0（低电平）= 吸合 true */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_read_fake.return_val = 0;
    bool out = false;
    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_GET(ops, ctx, &out));
    TEST_ASSERT_TRUE(out);
}

void test_relay_get_active_low_reads_level_one_as_off(void)
{
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_read_fake.return_val = 1;
    bool out = true;
    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_GET(ops, ctx, &out));
    TEST_ASSERT_FALSE(out);
}

void test_relay_get_active_high_reads_level_one_as_on(void)
{
    /* wiegand_pwr: active_high=true，GPIO 读 1 = 吸合 true */
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("wiegand_pwr", &ops, &ctx);

    pal_gpio_read_fake.return_val = 1;
    bool out = false;
    TEST_ASSERT_EQUAL(DAL_OK, DAL_RELAY_GET(ops, ctx, &out));
    TEST_ASSERT_TRUE(out);
}

/* ================================================================
 *  get PAL 错误翻译
 * ================================================================ */
void test_relay_get_pal_error_returns_hw_error(void)
{
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_read_fake.return_val = -1;   /* PAL 失败码 */
    bool out = false;
    TEST_ASSERT_EQUAL(DAL_ERR_HW, DAL_RELAY_GET(ops, ctx, &out));
}

void test_relay_get_null_out_param_returns_invalid(void)
{
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    /* 直接调 ops.get(ctx, NULL) 验证参数校验 */
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, ops->get(ctx, NULL));
}

/* ================================================================
 *  set PAL 错误翻译（不泄露原始码）
 * ================================================================ */
void test_relay_set_pal_error_returns_hw_error_not_raw(void)
{
    const dal_relay_ops_t *ops = NULL;
    void                  *ctx = NULL;
    dal_relay_get("door_lock", &ops, &ctx);

    pal_gpio_write_fake.return_val = -7;   /* 任意 PAL 失败码 */
    dal_err_t ret = DAL_RELAY_SET(ops, ctx, true);
    TEST_ASSERT_EQUAL(DAL_ERR_HW, ret);
    /* 确保不泄露原始 -7（DAL_ERR_HW = -6） */
    TEST_ASSERT_NOT_EQUAL(-7, ret);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bsp_relay_init_registers_three_relays);
    RUN_TEST(test_bsp_relay_set_pal_invalid_arg_translates_to_dal_invalid);

    RUN_TEST(test_relay_set_active_low_on_writes_low_level);
    RUN_TEST(test_relay_set_active_low_off_writes_high_level);
    RUN_TEST(test_relay_set_active_high_on_writes_high_level);

    RUN_TEST(test_relay_get_active_low_reads_level_zero_as_on);
    RUN_TEST(test_relay_get_active_low_reads_level_one_as_off);
    RUN_TEST(test_relay_get_active_high_reads_level_one_as_on);

    RUN_TEST(test_relay_get_pal_error_returns_hw_error);
    RUN_TEST(test_relay_get_null_out_param_returns_invalid);
    RUN_TEST(test_relay_set_pal_error_returns_hw_error_not_raw);

    return UNITY_END();
}
