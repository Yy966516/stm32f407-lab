#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

#define MODE_POLLING    0
#define MODE_INTERRUPT  1

/* 改这里切换：查询方式 / 中断方式 */
#define COMM_MODE MODE_POLLING
// #define COMM_MODE MODE_INTERRUPT

#define REQ_PORT GPIOG
#define REQ_PIN  GPIO_PIN_8

#define ACK_PORT GPIOG
#define ACK_PIN  GPIO_PIN_9

UART_HandleTypeDef huart1;

volatile uint32_t byte_count = 0;
volatile uint8_t last_data = 0;

static void GPIO_Init_All(void);
static void UART1_Init(void);
static void UART_Print(char *s);

static uint8_t Read_DataBus(void);
static void Show_LED(uint8_t data);
static uint8_t Parallel_Receive_Polling(uint8_t *pdata, uint32_t timeout_ms);

int main(void)
{
    HAL_Init();
    GPIO_Init_All();
    UART1_Init();

    char msg[120];

#if COMM_MODE == MODE_POLLING
    UART_Print("\r\n========== RX BOARD: POLLING MODE ==========\r\n");
#else
    UART_Print("\r\n========== RX BOARD: INTERRUPT MODE ==========\r\n");
#endif

    UART_Print("PD0~PD7: data bus input\r\n");
    UART_Print("PG8: REQ input\r\n");
    UART_Print("PG9: ACK output\r\n");
    UART_Print("PF6/PF7/PF8: LED follows low 3 bits\r\n");
    UART_Print("============================================\r\n");

    uint32_t last_count = 0;
    uint32_t last_tick = HAL_GetTick();

    while (1)
    {
#if COMM_MODE == MODE_POLLING
        uint8_t data = 0;

        if (Parallel_Receive_Polling(&data, 1000))
        {
            last_data = data;
            byte_count++;
            Show_LED(data);
        }
#endif

        /*
         * 每秒打印接收速度。
         * 查询方式：byte_count 在主循环增加。
         * 中断方式：byte_count 在 HAL_GPIO_EXTI_Callback 中增加。
         */
        uint32_t now = HAL_GetTick();

        if ((now - last_tick) >= 1000)
        {
            uint32_t diff = byte_count - last_count;
            uint32_t elapsed = now - last_tick;
            uint32_t speed_Bps = diff * 1000UL / elapsed;
            uint32_t speed_bps = speed_Bps * 8UL;

            sprintf(msg,
                    "[RX] data=0x%02X  total=%lu  speed=%lu B/s  (%lu bps)\r\n",
                    last_data,
                    (unsigned long)byte_count,
                    (unsigned long)speed_Bps,
                    (unsigned long)speed_bps);
            UART_Print(msg);

            last_count = byte_count;
            last_tick = now;
        }

#if COMM_MODE == MODE_INTERRUPT
        HAL_Delay(1);
#endif
    }
}

/* 读取 PD0~PD7 */
static uint8_t Read_DataBus(void)
{
    return (uint8_t)(GPIOD->IDR & 0x00FF);
}

/* PF6/PF7/PF8 LED 显示低三位。
 * 注意：板载 LED 通常低电平亮。
 */
static void Show_LED(uint8_t data)
{
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6,
                      (data & 0x01) ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_7,
                      (data & 0x02) ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_8,
                      (data & 0x04) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* 查询方式接收 */
static uint8_t Parallel_Receive_Polling(uint8_t *pdata, uint32_t timeout_ms)
{
    uint32_t timeout;

    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(REQ_PORT, REQ_PIN) == GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - timeout) > timeout_ms)
        {
            return 0;
        }
    }

    *pdata = Read_DataBus();

    HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_SET);

    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(REQ_PORT, REQ_PIN) == GPIO_PIN_SET)
    {
        if ((HAL_GetTick() - timeout) > timeout_ms)
        {
            HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_RESET);
            return 0;
        }
    }

    HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_RESET);

    return 1;
}

/* REQ 中断回调：接收端中断方式核心 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
#if COMM_MODE == MODE_INTERRUPT
    if (GPIO_Pin == REQ_PIN)
    {
        GPIO_PinState req = HAL_GPIO_ReadPin(REQ_PORT, REQ_PIN);

        if (req == GPIO_PIN_SET)
        {
            /*
             * REQ 上升沿：
             * 发送端通知数据有效。
             * 接收端读取数据，更新 LED，拉高 ACK。
             */
            uint8_t data = Read_DataBus();

            last_data = data;
            byte_count++;
            Show_LED(data);

            HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_SET);
        }
        else
        {
            /*
             * REQ 下降沿：
             * 发送端已经撤销 REQ。
             * 接收端拉低 ACK，准备下一帧。
             */
            HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_RESET);
        }
    }
#endif
}

static void UART_Print(char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 200);
}

static void GPIO_Init_All(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PD0~PD7：数据总线输入 */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

#if COMM_MODE == MODE_POLLING
    /* 查询方式：PG8 作为普通输入 */
    GPIO_InitStruct.Pin = REQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(REQ_PORT, &GPIO_InitStruct);
#else
    /* 中断方式：PG8 作为 REQ 双边沿中断输入 */
    GPIO_InitStruct.Pin = REQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(REQ_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif

    /* PG9：ACK 输出 */
    GPIO_InitStruct.Pin = ACK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(ACK_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(ACK_PORT, ACK_PIN, GPIO_PIN_RESET);

    /* PF6/PF7/PF8：LED 输出，默认灭 */
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8, GPIO_PIN_SET);
}

static void UART1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart1);
}