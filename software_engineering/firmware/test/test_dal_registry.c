/**
 * @file    test_dal_registry.c
 * @brief   dal_registry 公共注册表单元测试（宿主机 Mock 测试）
 *
 * 测试覆盖：
 *   - 初始化参数校验
 *   - 注册 / 查询基本流程
 *   - 重名注册拒绝
 *   - 注册表溢出
 *   - 未注册查询
 *   - 多实例共存独立查询
 *
 * @author  xLumina
 * @version 1.0
 */
#include "unity.h"
#include "dal_registry.h"
#include "dal_err.h"
#include <string.h>

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static dal_registry_t        s_reg;
static dal_registry_entry_t  s_entries[4];

void setUp(void)
{
    memset(&s_reg, 0, sizeof(s_reg));
    memset(s_entries, 0, sizeof(s_entries));
}

void tearDown(void)
{
}

/* ================================================================
 *  初始化参数校验
 * ================================================================ */
void test_registry_init_null_reg_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_init(NULL, s_entries, 4));
}

void test_registry_init_null_entries_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_init(&s_reg, NULL, 4));
}

void test_registry_init_zero_max_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_init(&s_reg, s_entries, 0));
}

void test_registry_init_valid_should_ok_and_zero_entries(void)
{
    TEST_ASSERT_EQUAL(DAL_OK, dal_registry_init(&s_reg, s_entries, 4));
    TEST_ASSERT_EQUAL(0, s_reg.count);
    TEST_ASSERT_EQUAL(4, s_reg.max);
}

/* ================================================================
 *  注册 / 查询基本流程
 * ================================================================ */
void test_registry_register_and_get_should_return_same_ops_ctx(void)
{
    dal_registry_init(&s_reg, s_entries, 4);

    int dummy_ops = 0;
    int dummy_ctx = 0;
    const void *ops_out = NULL;
    void       *ctx_out  = NULL;

    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_registry_register(&s_reg, "dev0", &dummy_ops, &dummy_ctx));
    TEST_ASSERT_EQUAL(1, s_reg.count);

    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_registry_get(&s_reg, "dev0", &ops_out, &ctx_out));
    TEST_ASSERT_EQUAL_PTR(&dummy_ops, ops_out);
    TEST_ASSERT_EQUAL_PTR(&dummy_ctx, ctx_out);
}

void test_registry_register_null_args_should_fail(void)
{
    dal_registry_init(&s_reg, s_entries, 4);
    int dummy = 0;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_register(&s_reg, NULL, &dummy, &dummy));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_register(&s_reg, "dev0", NULL, &dummy));
}

/* ================================================================
 *  重名注册
 * ================================================================ */
void test_registry_register_duplicate_name_should_fail(void)
{
    dal_registry_init(&s_reg, s_entries, 4);
    int dummy = 0;

    TEST_ASSERT_EQUAL(DAL_OK,
                      dal_registry_register(&s_reg, "dev0", &dummy, &dummy));
    TEST_ASSERT_EQUAL(DAL_ERR_STATE,
                      dal_registry_register(&s_reg, "dev0", &dummy, &dummy));
    TEST_ASSERT_EQUAL(1, s_reg.count);
}

/* ================================================================
 *  注册表溢出
 * ================================================================ */
void test_registry_register_overflow_should_fail(void)
{
    dal_registry_init(&s_reg, s_entries, 2);
    int dummy = 0;

    TEST_ASSERT_EQUAL(DAL_OK, dal_registry_register(&s_reg, "d0", &dummy, &dummy));
    TEST_ASSERT_EQUAL(DAL_OK, dal_registry_register(&s_reg, "d1", &dummy, &dummy));
    TEST_ASSERT_EQUAL(DAL_ERR_NO_MEM,
                      dal_registry_register(&s_reg, "d2", &dummy, &dummy));
    TEST_ASSERT_EQUAL(2, s_reg.count);
}

/* ================================================================
 *  未注册查询
 * ================================================================ */
void test_registry_get_not_found_should_return_null_and_clear_outputs(void)
{
    dal_registry_init(&s_reg, s_entries, 4);
    int dummy = 0;
    const void *ops_out = (const void *)0xDEAD;
    void       *ctx_out  = (void *)0xBEEF;

    /* 先注册一个，确保注册表非空 */
    dal_registry_register(&s_reg, "dev0", &dummy, &dummy);

    TEST_ASSERT_EQUAL(DAL_ERR_NOT_FOUND,
                      dal_registry_get(&s_reg, "absent", &ops_out, &ctx_out));
    TEST_ASSERT_NULL(ops_out);
    TEST_ASSERT_NULL(ctx_out);
}

void test_registry_get_invalid_args_should_fail(void)
{
    dal_registry_init(&s_reg, s_entries, 4);
    const void *ops_out = NULL;
    void       *ctx_out  = NULL;

    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_get(NULL, "x", &ops_out, &ctx_out));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_get(&s_reg, NULL, &ops_out, &ctx_out));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_get(&s_reg, "x", NULL, &ctx_out));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      dal_registry_get(&s_reg, "x", &ops_out, NULL));
}

/* ================================================================
 *  多实例共存独立查询
 * ================================================================ */
void test_registry_multiple_instances_independent(void)
{
    dal_registry_init(&s_reg, s_entries, 4);

    int ops_a = 1, ops_b = 2, ops_c = 3;
    int ctx_a = 10, ctx_b = 20, ctx_c = 30;

    dal_registry_register(&s_reg, "a", &ops_a, &ctx_a);
    dal_registry_register(&s_reg, "b", &ops_b, &ctx_b);
    dal_registry_register(&s_reg, "c", &ops_c, &ctx_c);

    const void *ops = NULL;
    void       *ctx = NULL;

    dal_registry_get(&s_reg, "a", &ops, &ctx);
    TEST_ASSERT_EQUAL_PTR(&ops_a, ops);
    TEST_ASSERT_EQUAL_PTR(&ctx_a, ctx);

    dal_registry_get(&s_reg, "b", &ops, &ctx);
    TEST_ASSERT_EQUAL_PTR(&ops_b, ops);
    TEST_ASSERT_EQUAL_PTR(&ctx_b, ctx);

    dal_registry_get(&s_reg, "c", &ops, &ctx);
    TEST_ASSERT_EQUAL_PTR(&ops_c, ops);
    TEST_ASSERT_EQUAL_PTR(&ctx_c, ctx);

    TEST_ASSERT_EQUAL(3, s_reg.count);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_registry_init_null_reg_should_fail);
    RUN_TEST(test_registry_init_null_entries_should_fail);
    RUN_TEST(test_registry_init_zero_max_should_fail);
    RUN_TEST(test_registry_init_valid_should_ok_and_zero_entries);

    RUN_TEST(test_registry_register_and_get_should_return_same_ops_ctx);
    RUN_TEST(test_registry_register_null_args_should_fail);

    RUN_TEST(test_registry_register_duplicate_name_should_fail);

    RUN_TEST(test_registry_register_overflow_should_fail);

    RUN_TEST(test_registry_get_not_found_should_return_null_and_clear_outputs);
    RUN_TEST(test_registry_get_invalid_args_should_fail);

    RUN_TEST(test_registry_multiple_instances_independent);

    return UNITY_END();
}
