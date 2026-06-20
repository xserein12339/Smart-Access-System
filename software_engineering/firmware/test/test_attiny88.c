/**
 * @file    test_attiny88.c
 * @brief   ATTINY88 背光/电源 MCU 子驱动单元测试（宿主机 Mock 测试）
 *
 * @details 验证 ATTINY88 上电序列的寄存器写入正确性：
 *            init   → PORTC/PORTB/PWM 清零
 *            power_on → PORTA(扫描) + PORTB(主电源) + PORTC(LED_EN)
 *            release_reset → PORTC(LED_EN|LCD_RST|BRIDGE_RST|TOUCH_RST)
 *            set_backlight → PWM 寄存器按百分比映射
 *          pal_i2c_write 用 custom fake 记录 (reg,val) 序列。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头（必须在真实头之前）---- */
#include "esp_err.h"
#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"

/* ---- 被测模块 ---- */
#include "bsp_display_attiny88.h"
#include "bsp_config.h"

/* ================================================================
 *  custom fake：记录 pal_i2c_write 的 (reg, val) 写入序列
 * ================================================================ */
#define MAX_WRITE_RECORDS 64
typedef struct { uint8_t reg; uint8_t val; } write_record_t;
static write_record_t s_writes[MAX_WRITE_RECORDS];
static int            s_write_count;

/** 记录最近一次 attach 的设备地址（cfg 为调用方栈变量，勿存指针） */
static uint16_t s_last_attach_addr;

/** custom fake：pal_i2c_dev_attach 写 *dev 为非 NULL 占位 */
static int attach_sets_dev(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                           const pal_i2c_dev_config_t *cfg)
{
    (void)bus;
    if (dev) *dev = (pal_i2c_dev_handle_t)1;
    if (cfg) s_last_attach_addr = cfg->device_address;
    return 0;
}

static int write_recorder(pal_i2c_dev_handle_t dev, const uint8_t *data, size_t len)
{
    (void)dev;
    if (data != NULL && len >= 2 && s_write_count < MAX_WRITE_RECORDS) {
        s_writes[s_write_count].reg = data[0];
        s_writes[s_write_count].val = data[1];
        s_write_count++;
    }
    return 0;
}

/** 查找某寄存器最后一次写入值（-1 未找到） */
static int find_last_write(uint8_t reg)
{
    for (int i = s_write_count - 1; i >= 0; i--) {
        if (s_writes[i].reg == reg) return s_writes[i].val;
    }
    return -1;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bsp_attiny88_ctx_t s_ctx;

void setUp(void)
{
    RESET_FAKE(pal_i2c_dev_attach);
    RESET_FAKE(pal_i2c_dev_detach);
    RESET_FAKE(pal_i2c_write);
    RESET_FAKE(pal_i2c_read_reg);

    memset(s_writes, 0, sizeof(s_writes));
    s_write_count = 0;
    s_last_attach_addr = 0;
    memset(&s_ctx, 0, sizeof(s_ctx));

    pal_i2c_dev_attach_fake.return_val = 0;
    pal_i2c_dev_attach_fake.custom_fake = attach_sets_dev;
    pal_i2c_dev_detach_fake.return_val = 0;
    pal_i2c_read_reg_fake.return_val   = 0;
    pal_i2c_write_fake.custom_fake     = write_recorder;
}

void tearDown(void) {}

/* ================================================================
 *  init
 * ================================================================ */
void test_attiny88_init_attaches_i2c_dev_and_clears_state(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_init(&s_ctx, bus));

    /* 应挂载 I2C 设备，地址取自板级配置 */
    TEST_ASSERT_EQUAL(1, pal_i2c_dev_attach_fake.call_count);
    TEST_ASSERT_EQUAL(BOARD_DISPLAY_ATTINY88_I2C_ADDR, s_last_attach_addr);

    /* 初始状态：PORTC/PORTB/PWM 清零 */
    TEST_ASSERT_EQUAL(0x00, find_last_write(ATTINY88_REG_PORTC));
    TEST_ASSERT_EQUAL(0x00, find_last_write(ATTINY88_REG_PORTB));
    TEST_ASSERT_EQUAL(0x00, find_last_write(ATTINY88_REG_PWM));
    TEST_ASSERT_TRUE(s_ctx.inited);
}

void test_attiny88_init_null_args_should_fail(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_attiny88_init(NULL, bus));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_attiny88_init(&s_ctx, NULL));
}

void test_attiny88_init_attach_fail_returns_hw_error(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    /* 清除 custom_fake 使 return_val 生效，模拟 attach 失败 */
    pal_i2c_dev_attach_fake.custom_fake = NULL;
    pal_i2c_dev_attach_fake.return_val = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_attiny88_init(&s_ctx, bus));
    TEST_ASSERT_FALSE(s_ctx.inited);
}

/* ================================================================
 *  power_on 序列
 * ================================================================ */
void test_attiny88_power_on_sets_porta_portb_portc(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;   /* 仅观察 power_on 的写入 */

    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_power_on(&s_ctx));

    /* PORTA: 扫描方向 bit2 */
    TEST_ASSERT_EQUAL(ATTINY88_PORTA_SCAN_LR, find_last_write(ATTINY88_REG_PORTA));
    /* PORTB: 主电源 bit7 */
    TEST_ASSERT_EQUAL(ATTINY88_PORTB_POWER_ON, find_last_write(ATTINY88_REG_PORTB));
    /* PORTC: 仅 LED_EN（桥/LCD/触控复位仍保持低） */
    TEST_ASSERT_EQUAL(ATTINY88_PORTC_LED_EN, find_last_write(ATTINY88_REG_PORTC));
}

void test_attiny88_power_on_not_inited_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_attiny88_power_on(&s_ctx));
}

/* ================================================================
 *  release_reset 序列
 * ================================================================ */
void test_attiny88_release_reset_sets_all_reset_bits(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;

    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_release_reset(&s_ctx));

    /* PORTC: LED_EN + LCD_RST + BRIDGE_RST + TOUCH_RST 全部置 1（释放复位） */
    uint8_t expected = ATTINY88_PORTC_LED_EN
                     | ATTINY88_PORTC_LCD_RST
                     | ATTINY88_PORTC_BRIDGE_RST
                     | ATTINY88_PORTC_TOUCH_RST;
    TEST_ASSERT_EQUAL(expected, find_last_write(ATTINY88_REG_PORTC));
}

/* ================================================================
 *  set_backlight 百分比映射
 * ================================================================ */
void test_attiny88_set_backlight_100_percent_maps_to_max(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;

    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_set_backlight(&s_ctx, 100));
    /* attiny88 PWM 满量程 255：100 * 255 / 100 = 255 */
    TEST_ASSERT_EQUAL(255, find_last_write(ATTINY88_REG_PWM));
}

void test_attiny88_set_backlight_0_percent_maps_to_zero(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;

    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_set_backlight(&s_ctx, 0));
    TEST_ASSERT_EQUAL(0x00, find_last_write(ATTINY88_REG_PWM));
}

void test_attiny88_set_backlight_50_percent_maps_to_mid(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;

    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_set_backlight(&s_ctx, 50));
    /* 50 * 255 / 100 = 127 = 0x7F */
    TEST_ASSERT_EQUAL(0x7F, find_last_write(ATTINY88_REG_PWM));
}

void test_attiny88_set_backlight_not_inited_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_attiny88_set_backlight(&s_ctx, 50));
}

/* ================================================================
 *  deinit
 * ================================================================ */
void test_attiny88_deinit_detaches_i2c_dev(void)
{
    bsp_attiny88_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    TEST_ASSERT_EQUAL(DAL_OK, bsp_attiny88_deinit(&s_ctx));
    TEST_ASSERT_EQUAL(1, pal_i2c_dev_detach_fake.call_count);
    TEST_ASSERT_FALSE(s_ctx.inited);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_attiny88_init_attaches_i2c_dev_and_clears_state);
    RUN_TEST(test_attiny88_init_null_args_should_fail);
    RUN_TEST(test_attiny88_init_attach_fail_returns_hw_error);

    RUN_TEST(test_attiny88_power_on_sets_porta_portb_portc);
    RUN_TEST(test_attiny88_power_on_not_inited_should_fail);

    RUN_TEST(test_attiny88_release_reset_sets_all_reset_bits);

    RUN_TEST(test_attiny88_set_backlight_100_percent_maps_to_max);
    RUN_TEST(test_attiny88_set_backlight_0_percent_maps_to_zero);
    RUN_TEST(test_attiny88_set_backlight_50_percent_maps_to_mid);
    RUN_TEST(test_attiny88_set_backlight_not_inited_should_fail);

    RUN_TEST(test_attiny88_deinit_detaches_i2c_dev);

    return UNITY_END();
}
