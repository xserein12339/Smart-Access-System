# 公共 SOC caps compile_definitions + 时钟源类型桩
# linux target 的 soc/soc_caps.h 与 soc/clk_tree_defs.h 对 uart/ledc/i2s/i2c 支持不全，
# 导致驱动头文件中的类型/字段/枚举缺失。此处注入 esp32p4 对应 caps 子集 + 强制 include
# 时钟源桩头，使 CMock 能解析头文件、SUT 能编译。
#
# 用法：在需要编译这些驱动头的 target 上调用 apply_soc_caps(<target>)

set(PAL_HOST_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "PAL host_test 根目录")

function(apply_soc_caps target)
    target_compile_definitions(${target} PRIVATE
        # UART
        SOC_UART_SUPPORTED=1
        SOC_UART_NUM=2
        SOC_UART_HP_NUM=2
        SOC_UART_LP_NUM=0
        SOC_UART_FIFO_LEN=128
        SOC_UART_WAKEUP_SUPPORT_ACTIVE_THRESH_MODE=1
        SOC_UART_WAKEUP_SUPPORT_FIFO_THRESH_MODE=1
        SOC_UART_WAKEUP_SUPPORT_START_BIT_MODE=1
        SOC_UART_WAKEUP_SUPPORT_CHAR_SEQ_MODE=1
        # LEDC
        SOC_LEDC_SUPPORTED=1
        SOC_LEDC_TIMER_NUM=4
        SOC_LEDC_CHANNEL_NUM=8
        SOC_LEDC_TIMER_BIT_WIDTH=20
        SOC_LEDC_SUPPORT_HS_MODE=0
        SOC_LEDC_SUPPORT_APB_CLOCK=1
        SOC_LEDC_SUPPORT_PLL_DIV_CLOCK=1
        SOC_LEDC_SUPPORT_XTAL_CLOCK=1
        SOC_LEDC_SUPPORT_REF_TICK=0
        SOC_LEDC_SUPPORT_FADE_STOP=1
        SOC_LEDC_GAMMA_CURVE_FADE_SUPPORTED=1
        SOC_LEDC_FADE_PARAMS_BIT_WIDTH=10
        # I2S
        SOC_I2S_SUPPORTED=1
        SOC_I2S_NUM=2
        SOC_I2S_HW_VERSION_2=1
        SOC_I2S_SUPPORTS_PCM=1
        SOC_I2S_SUPPORTS_PDM=1
        SOC_I2S_SUPPORTS_PDM_RX=1
        SOC_I2S_SUPPORTS_PDM_TX=1
        SOC_I2S_SUPPORTS_TDM=1
        SOC_I2S_SUPPORTS_XTAL=1
        # I2C
        SOC_I2C_SUPPORTED=1
        SOC_I2C_NUM=2
        SOC_HP_I2C_NUM=2
        SOC_I2C_FIFO_LEN=32)

    # 强制在编译单元开头 include 时钟源类型桩（补 soc_periph_*_clk_src_t）
    target_compile_options(${target} PRIVATE
        -include "${PAL_HOST_TEST_DIR}/stubs/clk_src_stubs.h")
endfunction()
