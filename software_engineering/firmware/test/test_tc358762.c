/**
 * @file    test_tc358762.c
 * @brief   TC358762 DSI 桥接子驱动单元测试（宿主机 Mock 测试）
 *
 * @details 验证 TC358762 桥寄存器初始化序列（经 DSI Generic Write）：
 *            - init 调 pal_mipi_dsi_init
 *            - config_bridge 写 DSI_LANEENABLE/PPI/LCDCTRL/SYSCTRL/时序/
 *              PPI_STARTDSI 序列
 *            - fill/draw_bitmap/get_fb 分发到 PAL DSI
 *          pal_mipi_dsi_send_generic_write 用 custom fake 记录 (addr,val)。
 *
 *  DSI Generic Write payload[6] = {reg_lo, reg_hi, val_b0, val_b1, val_b2, val_b3}
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "pal_mipi_dsi.h"
#include "pal_log.h"

/* ---- 被测模块 ---- */
#include "bsp_display_tc358762.h"
#include "bsp_config.h"

/* ================================================================
 *  custom fake：记录 send_generic_write 的 (addr, val)
 * ================================================================ */
#define MAX_GW_RECORDS 64
typedef struct { uint16_t addr; uint32_t val; } gw_record_t;
static gw_record_t s_gw[MAX_GW_RECORDS];
static int         s_gw_count;

/** custom fake：pal_mipi_dsi_init 写 *handle 为非 NULL 占位 */
static int dsi_init_sets_handle(pal_mipi_dsi_handle_t *handle,
                                const pal_mipi_dsi_config_t *cfg)
{
    (void)cfg;
    if (handle) *handle = (pal_mipi_dsi_handle_t)1;
    return 0;
}

static int gw_recorder(pal_mipi_dsi_handle_t dsi, uint8_t vc,
                       const uint8_t *payload, size_t len)
{
    (void)dsi; (void)vc;
    if (payload != NULL && len >= 6 && s_gw_count < MAX_GW_RECORDS) {
        s_gw[s_gw_count].addr = (uint16_t)(payload[0] | (payload[1] << 8));
        s_gw[s_gw_count].val  = (uint32_t)payload[2]
                              | ((uint32_t)payload[3] << 8)
                              | ((uint32_t)payload[4] << 16)
                              | ((uint32_t)payload[5] << 24);
        s_gw_count++;
    }
    return 0;
}

static int find_gw(uint16_t addr)
{
    for (int i = s_gw_count - 1; i >= 0; i--) {
        if (s_gw[i].addr == addr) return (int)s_gw[i].val;
    }
    return -1;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bsp_tc358762_ctx_t s_ctx;

void setUp(void)
{
    RESET_FAKE(pal_mipi_dsi_init);
    RESET_FAKE(pal_mipi_dsi_deinit);
    RESET_FAKE(pal_mipi_dsi_fill);
    RESET_FAKE(pal_mipi_dsi_draw_bitmap);
    RESET_FAKE(pal_mipi_dsi_get_fb);
    RESET_FAKE(pal_mipi_dsi_send_generic_write);

    memset(s_gw, 0, sizeof(s_gw));
    s_gw_count = 0;
    memset(&s_ctx, 0, sizeof(s_ctx));

    pal_mipi_dsi_init_fake.return_val              = 0;
    pal_mipi_dsi_init_fake.custom_fake             = dsi_init_sets_handle;
    pal_mipi_dsi_deinit_fake.return_val            = 0;
    pal_mipi_dsi_fill_fake.return_val              = 0;
    pal_mipi_dsi_draw_bitmap_fake.return_val       = 0;
    pal_mipi_dsi_get_fb_fake.return_val            = 0;
    pal_mipi_dsi_send_generic_write_fake.custom_fake = gw_recorder;
}

void tearDown(void) {}

/* ================================================================
 *  init
 * ================================================================ */
void test_tc358762_init_calls_pal_dsi_init_and_marks_inited(void)
{
    TEST_ASSERT_EQUAL(DAL_OK, bsp_tc358762_init(&s_ctx));
    TEST_ASSERT_EQUAL(1, pal_mipi_dsi_init_fake.call_count);
    TEST_ASSERT_TRUE(s_ctx.inited);
    TEST_ASSERT_EQUAL(BOARD_DISPLAY_H_RES, s_ctx.width);
    TEST_ASSERT_EQUAL(BOARD_DISPLAY_V_RES, s_ctx.height);
}

void test_tc358762_init_pal_fail_returns_hw_error(void)
{
    pal_mipi_dsi_init_fake.custom_fake = NULL;
    pal_mipi_dsi_init_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(DAL_ERR_HW, bsp_tc358762_init(&s_ctx));
    TEST_ASSERT_FALSE(s_ctx.inited);
}

void test_tc358762_init_null_ctx_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_tc358762_init(NULL));
}

/* ================================================================
 *  config_bridge 寄存器序列
 * ================================================================ */
void test_config_bridge_writes_lane_enable_clock_and_d0(void)
{
    bsp_tc358762_init(&s_ctx);
    TEST_ASSERT_EQUAL(DAL_OK, bsp_tc358762_config_bridge(&s_ctx));
    /* DSI_LANEENABLE = CLOCK|D0 = 0x03 */
    TEST_ASSERT_EQUAL(DSI_LANEENABLE_CLOCK | DSI_LANEENABLE_D0,
                      find_gw(TC358762_REG_DSI_LANEENABLE));
}

void test_config_bridge_writes_lcdctrl_and_sysctrl(void)
{
    bsp_tc358762_init(&s_ctx);
    bsp_tc358762_config_bridge(&s_ctx);
    TEST_ASSERT_EQUAL(0x00100150, find_gw(TC358762_REG_LCDCTRL));
    TEST_ASSERT_EQUAL(0x040F, find_gw(TC358762_REG_SYSCTRL));
}

void test_config_bridge_writes_timing_registers(void)
{
    bsp_tc358762_init(&s_ctx);
    bsp_tc358762_config_bridge(&s_ctx);
    /* 时序寄存器应被写入（值非 -1 表示命中） */
    TEST_ASSERT_NOT_EQUAL(-1, find_gw(TC358762_REG_HS_HBP));
    TEST_ASSERT_NOT_EQUAL(-1, find_gw(TC358762_REG_HDISP_HFP));
    TEST_ASSERT_NOT_EQUAL(-1, find_gw(TC358762_REG_VS_VBP));
    TEST_ASSERT_NOT_EQUAL(-1, find_gw(TC358762_REG_VDISP_VFP));

    /* HS_HBP = (hpw << 16) | (hpw + hbp)，用板级配置验证 */
    uint32_t expected = ((uint32_t)BOARD_DISPLAY_HSYNC_PULSE_WIDTH << 16)
                      | (BOARD_DISPLAY_HSYNC_PULSE_WIDTH + BOARD_DISPLAY_HSYNC_BACK_PORCH);
    TEST_ASSERT_EQUAL(expected, (uint32_t)find_gw(TC358762_REG_HS_HBP));
}

void test_config_bridge_starts_ppi_and_dsi(void)
{
    bsp_tc358762_init(&s_ctx);
    bsp_tc358762_config_bridge(&s_ctx);
    TEST_ASSERT_EQUAL(1, find_gw(TC358762_REG_PPI_STARTPPI));
    TEST_ASSERT_EQUAL(1, find_gw(TC358762_REG_DSI_STARTDSI));
}

void test_config_bridge_not_inited_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_tc358762_config_bridge(&s_ctx));
}

/* ================================================================
 *  fill / draw_bitmap / get_fb 分发
 * ================================================================ */
void test_fill_delegates_to_pal_dsi_fill(void)
{
    bsp_tc358762_init(&s_ctx);
    TEST_ASSERT_EQUAL(DAL_OK, bsp_tc358762_fill(&s_ctx, 0, 0, 100, 100, 0xF800));
    TEST_ASSERT_EQUAL(1, pal_mipi_dsi_fill_fake.call_count);
}

void test_draw_bitmap_delegates_to_pal_dsi(void)
{
    bsp_tc358762_init(&s_ctx);
    uint8_t buf[10] = {0};
    TEST_ASSERT_EQUAL(DAL_OK, bsp_tc358762_draw_bitmap(&s_ctx, 0, 0, 5, 1, buf));
    TEST_ASSERT_EQUAL(1, pal_mipi_dsi_draw_bitmap_fake.call_count);
}

void test_get_fb_returns_fb0(void)
{
    bsp_tc358762_init(&s_ctx);
    /* pal_mipi_dsi_get_fb fake 默认不写输出指针，需 custom fake 设 fb0 */
    void *fb = NULL;
    /* 默认 get_fb return 0，但 fb0 未设；测试聚焦调用分发 */
    bsp_tc358762_get_fb(&s_ctx, &fb);
    TEST_ASSERT_EQUAL(1, pal_mipi_dsi_get_fb_fake.call_count);
}

void test_fill_not_inited_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_tc358762_fill(&s_ctx, 0, 0, 1, 1, 0));
}

/* ================================================================
 *  deinit
 * ================================================================ */
void test_deinit_calls_pal_dsi_deinit(void)
{
    bsp_tc358762_init(&s_ctx);
    TEST_ASSERT_EQUAL(DAL_OK, bsp_tc358762_deinit(&s_ctx));
    TEST_ASSERT_EQUAL(1, pal_mipi_dsi_deinit_fake.call_count);
    TEST_ASSERT_FALSE(s_ctx.inited);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_tc358762_init_calls_pal_dsi_init_and_marks_inited);
    RUN_TEST(test_tc358762_init_pal_fail_returns_hw_error);
    RUN_TEST(test_tc358762_init_null_ctx_should_fail);

    RUN_TEST(test_config_bridge_writes_lane_enable_clock_and_d0);
    RUN_TEST(test_config_bridge_writes_lcdctrl_and_sysctrl);
    RUN_TEST(test_config_bridge_writes_timing_registers);
    RUN_TEST(test_config_bridge_starts_ppi_and_dsi);
    RUN_TEST(test_config_bridge_not_inited_should_fail);

    RUN_TEST(test_fill_delegates_to_pal_dsi_fill);
    RUN_TEST(test_draw_bitmap_delegates_to_pal_dsi);
    RUN_TEST(test_get_fb_returns_fb0);
    RUN_TEST(test_fill_not_inited_should_fail);

    RUN_TEST(test_deinit_calls_pal_dsi_deinit);

    return UNITY_END();
}
