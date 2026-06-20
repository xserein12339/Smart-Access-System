/**
 * @file    test_dal_relay.c
 * @brief   dal_relay 管理 API 单元测试（宿主机 Mock 测试）
 *
 * @details 测试 dal_relay 的注册 / 查询管理逻辑（基于其自带注册表 +
 *          mock osal_mutex）。不依赖 BSP 与硬件，ops 用栈上 mock。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "unity.h"
#include "dal_relay.h"
#include "dal_relay_interface.h"
#include "dal_err.h"

/* ---- 栈上 mock ops（无需真实硬件行为）---- */
static dal_err_t mock_set(void *ctx, bool on)
{
    (void)ctx; (void)on;
    return DAL_OK;
}
static dal_err_t mock_get(void *ctx, bool *out)
{
    (void)ctx;
    if (out) *out = false;
    return DAL_OK;
}
static const dal_relay_ops_t s_mock_ops = {
    .set = mock_set,
    .get = mock_get,
};

/* ================================================================
 *  setUp / tearDown
 * ================================================================
 * dal_relay 用静态全局注册表，跨用例累积。setUp 不重置（注册表无公开
 * reset API），用例通过唯一名称避免相互干扰。
 */
void setUp(void) {}
void tearDown(void) {}

/* ================================================================
 *  注册 / 查询基本流程
 * ================================================================ */
void test_relay_register_and_get_should_return_same_ops_ctx(void)
{
    int ctx = 42;
    const dal_relay_ops_t *ops_out = NULL;
    void                  *ctx_out  = NULL;

    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_relay_register("relay_basic", &s_mock_ops, &ctx));

    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_relay_get("relay_basic", &ops_out, &ctx_out));
    TEST_ASSERT_EQUAL_PTR(&s_mock_ops, ops_out);
    TEST_ASSERT_EQUAL_PTR(&ctx, ctx_out);
}

void test_relay_register_null_args_should_fail(void)
{
    int ctx = 0;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_relay_register(NULL, &s_mock_ops, &ctx));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_relay_register("relay_null", NULL, &ctx));
}

/* ================================================================
 *  重名注册
 * ================================================================ */
void test_relay_register_duplicate_should_fail(void)
{
    int ctx = 0;
    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_relay_register("relay_dup", &s_mock_ops, &ctx));
    TEST_ASSERT_EQUAL(DAL_ERR_STATE,
                      dal_relay_register("relay_dup", &s_mock_ops, &ctx));
}

/* ================================================================
 *  未注册查询
 * ================================================================ */
void test_relay_get_not_found_should_return_null_outputs(void)
{
    const dal_relay_ops_t *ops_out = (const dal_relay_ops_t *)0xDEAD;
    void                  *ctx_out  = (void *)0xBEEF;

    TEST_ASSERT_EQUAL(DAL_ERR_NOT_FOUND,
                      dal_relay_get("relay_absent", &ops_out, &ctx_out));
    TEST_ASSERT_NULL(ops_out);
    TEST_ASSERT_NULL(ctx_out);
}

void test_relay_get_null_args_should_fail(void)
{
    const dal_relay_ops_t *ops_out = NULL;
    void                  *ctx_out  = NULL;

    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_relay_get(NULL, &ops_out, &ctx_out));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_relay_get("relay_x", NULL, &ctx_out));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_relay_get("relay_x", &ops_out, NULL));
}

/* ================================================================
 *  原子性：get 同时返回 ops 与 ctx（一致性）
 * ================================================================ */
void test_relay_get_returns_consistent_ops_and_ctx(void)
{
    int ctx = 99;
    const dal_relay_ops_t *ops_out = NULL;
    void                  *ctx_out  = NULL;

    dal_relay_register("relay_consistent", &s_mock_ops, &ctx);
    dal_relay_get("relay_consistent", &ops_out, &ctx_out);

    /* ops 与 ctx 必须指向同一注册条目，不可错配 */
    TEST_ASSERT_EQUAL_PTR(&s_mock_ops, ops_out);
    TEST_ASSERT_EQUAL_PTR(&ctx, ctx_out);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_relay_register_and_get_should_return_same_ops_ctx);
    RUN_TEST(test_relay_register_null_args_should_fail);
    RUN_TEST(test_relay_register_duplicate_should_fail);
    RUN_TEST(test_relay_get_not_found_should_return_null_outputs);
    RUN_TEST(test_relay_get_null_args_should_fail);
    RUN_TEST(test_relay_get_returns_consistent_ops_and_ctx);

    return UNITY_END();
}
