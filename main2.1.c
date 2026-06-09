/* 
 * 硬件：STM32F407 霸天虎V2
 *
 * 中断分配：
 *   PG9  → EXTI9_5_IRQn  优先级 2（计算/等号）
 *   PG10 → EXTI15_10_IRQn 优先级 1（清零，更高优先级）
 *                          ↑ 可以打断PG9的中断 = 中断嵌套
 *
 * 查询分配：
 *   PG6  查询（输入/锁存）
 *   PG8  查询（切换输入B）
 * ============================================================ */

#include "stm32f4xx_hal.h"

/* ============================================================
 * 共阴极数码管段码表
 * bit0=a bit1=b ... bit6=g
 * ============================================================ */
static const uint8_t SEG_CODE[10] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F  /* 9 */
};

/* ============================================================
 * 状态机
 * ============================================================ */
typedef enum {
    STATE_INPUT_A = 0,
    STATE_INPUT_B,
    STATE_SHOW_RESULT
} CalcState;

static volatile CalcState state  = STATE_INPUT_A;
static volatile uint8_t   val_A  = 0;
static volatile uint8_t   val_B  = 0;
static volatile uint8_t   result = 0;

/* ============================================================
 * 函数声明
 * ============================================================ */
static void SystemClock_Config(void);
static void GPIO_Init(void);

static uint8_t Read_DIP(void);
static uint8_t Read_Key_PG6(void);
static uint8_t Read_Key_PG8(void);

static void Display_Tens(uint8_t digit);
static void Display_Units(uint8_t digit);
static void Set_LED(uint8_t on);
static void Delay_ms(uint32_t ms);

/* ============================================================
 * 主函数
 * ============================================================ */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();

    /* 上电显示00，LED灭 */
    Display_Tens(0);
    Display_Units(0);
    Set_LED(0);

    while (1)
    {
        /* ====================================================
         * PG6：输入键（查询方式）
         * 锁存当前拨码值到 val_A 或 val_B
         * ==================================================== */
        if (Read_Key_PG6())
        {
            uint8_t dip = Read_DIP();
            if (dip > 9) dip = 9;

            if (state == STATE_INPUT_A)
            {
                val_A = dip;
                Display_Tens(0);
                Display_Units(val_A);
            }
            else if (state == STATE_INPUT_B)
            {
                val_B = dip;
                Display_Tens(0);
                Display_Units(val_B);
            }
            Delay_ms(300);
        }

        /* ====================================================
         * PG8：加法键（查询方式）
         * 切换到输入B状态
         * ==================================================== */
        if (Read_Key_PG8())
        {
            if (state == STATE_INPUT_A)
            {
                state = STATE_INPUT_B;
                Display_Tens(0);
                Display_Units(0);
            }
            Delay_ms(300);
        }
    }
}

/* ============================================================
 * GPIO初始化
 * ============================================================ */
static void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* --------------------------------------------------------
     * 十位数码管：PB0 PB1 PB3 PB4 PB5 PB6 PB7（跳过PB2）
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 |
        GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIOB->ODR &= ~0x00FB;

    /* --------------------------------------------------------
     * PB15：溢出LED
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);

    /* --------------------------------------------------------
     * 个位数码管：PE7~PE13
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin =
        GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  |
        GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    GPIOE->ODR &= ~0x3F80;

    /* --------------------------------------------------------
     * 拨码开关：PD0~PD3，内部上拉输入
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* --------------------------------------------------------
     * PG6 PG8：查询按键，内部上拉输入
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin  = GPIO_PIN_6 | GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* --------------------------------------------------------
     * PG9：外部中断，下降沿触发（计算/等号）
     * 优先级 2，次高
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin  = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /* --------------------------------------------------------
     * PG10：外部中断，下降沿触发（清零）
     * 优先级 1，最高 → 可以打断PG9的中断 = 中断嵌套
     * -------------------------------------------------------- */
    GPIO_InitStruct.Pin  = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ============================================================
 * 读取拨码开关（低电平有效）
 * ============================================================ */
static uint8_t Read_DIP(void)
{
    uint8_t val = 0;

    if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_RESET) val |= (1 << 0);
    if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1) == GPIO_PIN_RESET) val |= (1 << 1);
    if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2) == GPIO_PIN_RESET) val |= (1 << 2);
    if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_3) == GPIO_PIN_RESET) val |= (1 << 3);

    return val;
}

/* ============================================================
 * 查询按键
 * ============================================================ */
static uint8_t Read_Key_PG6(void)
{
    if (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6) == GPIO_PIN_RESET)
    {
        Delay_ms(20);
        if (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6) == GPIO_PIN_RESET)
            return 1;
    }
    return 0;
}

static uint8_t Read_Key_PG8(void)
{
    if (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_8) == GPIO_PIN_RESET)
    {
        Delay_ms(20);
        if (HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_8) == GPIO_PIN_RESET)
            return 1;
    }
    return 0;
}

/* ============================================================
 * 十位显示（PB0=a PB1=b PB3=c PB4=d PB5=e PB6=f PB7=g）
 * ============================================================ */
static void Display_Tens(uint8_t digit)
{
    uint8_t  code = SEG_CODE[digit % 10];
    uint32_t odr  = GPIOB->ODR;

    odr &= ~0x00FB;

    if (code & (1 << 0)) odr |= GPIO_PIN_0;
    if (code & (1 << 1)) odr |= GPIO_PIN_1;
    if (code & (1 << 2)) odr |= GPIO_PIN_3;
    if (code & (1 << 3)) odr |= GPIO_PIN_4;
    if (code & (1 << 4)) odr |= GPIO_PIN_5;
    if (code & (1 << 5)) odr |= GPIO_PIN_6;
    if (code & (1 << 6)) odr |= GPIO_PIN_7;

    GPIOB->ODR = odr;
}

/* ============================================================
 * 个位显示（PE7=a ... PE13=g）
 * ============================================================ */
static void Display_Units(uint8_t digit)
{
    uint8_t  code = SEG_CODE[digit % 10];
    uint32_t odr  = GPIOE->ODR;

    odr &= ~0x3F80;
    odr |= ((uint32_t)(code & 0x7F) << 7);

    GPIOE->ODR = odr;
}

/* ============================================================
 * LED控制（PB15）
 * ============================================================ */
static void Set_LED(uint8_t on)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ============================================================
 * 毫秒延时
 * ============================================================ */
static void Delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ============================================================
 * 统一中断回调
 * PG9  → 计算并显示结果（优先级2）
 * PG10 → 清零（优先级1，可打断PG9 = 中断嵌套）
 * ============================================================ */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* ------ PG9：计算（等号键）------ */
    if (GPIO_Pin == GPIO_PIN_9)
    {
        result = val_A + val_B;
        state  = STATE_SHOW_RESULT;

        Display_Tens(result / 10);
        Display_Units(result % 10);
        Set_LED(result >= 10 ? 1 : 0);
    }

    /* ------ PG10：清零（可打断上面的计算中断）------ */
    if (GPIO_Pin == GPIO_PIN_10)
    {
        val_A  = 0;
        val_B  = 0;
        result = 0;
        state  = STATE_INPUT_A;

        Display_Tens(0);
        Display_Units(0);
        Set_LED(0);
    }
}

/* ============================================================
 * 中断服务函数
 * ============================================================ */

/* PG9 → EXTI9_5，优先级2 */
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}

/* PG10 → EXTI15_10，优先级1（更高，可嵌套进EXTI9_5） */
void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
}

/* ============================================================
 * 系统时钟 168MHz（HSE 25MHz）
 * ============================================================ */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}