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

/* 中断发送状态 */
volatile uint8_t tx_busy = 0;
volatile uint8_t tx_done = 0;
volatile uint8_t tx_error = 0;
volatile uint8_t tx_state = 0;
/*
 * tx_state = 0 空闲
 * tx_state = 1 已拉高 REQ，等待 ACK=1
 * tx_state = 2 已拉低 REQ，等待 ACK=0
 */
volatile uint32_t tx_tick = 0;

static void GPIO_Init_All(void);
static void UART1_Init(void);
static void UART_Print(char *s);

static uint8_t Read_Switch(void);
static void Write_DataBus(uint8_t data);

static uint8_t Parallel_Send_Polling(uint8_t data, uint32_t timeout_ms);
static uint8_t Parallel_Send_IT_Blocking(uint8_t data, uint32_t timeout_ms);
static uint8_t Parallel_Send(uint8_t data, uint32_t timeout_ms);

int main(void)
{
    HAL_Init();
    GPIO_Init_All();
    UART1_Init();

    char msg[120];

#if COMM_MODE == MODE_POLLING
    UART_Print("\r\n========== TX BOARD: POLLING MODE ==========\r\n");
#else
    UART_Print("\r\n========== TX BOARD: INTERRUPT MODE ==========\r\n");
#endif

    UART_Print("PC0~PC3: switch input\r\n");
    UART_Print("PD0~PD7: data bus output\r\n");
    UART_Print("PG8: REQ output\r\n");
    UART_Print("PG9: ACK input\r\n");
    UART_Print("PA0: speed test key\r\n");
    UART_Print("============================================\r\n");

    uint8_t last_data = 0xFF;
    uint32_t last_send_tick = 0;
    uint32_t normal_count = 0;

    while (1)
    {
        uint8_t data = Read_Switch();

        /*
         * 正常演示通信：
         * 1. 拨码变化时发送；
         * 2. 每隔 300ms 重发一次，方便接收端 LED 稳定跟随。
         */
        if ((data != last_data) || (HAL_GetTick() - last_send_tick >= 300))
        {
            if (Parallel_Send(data, 1000))
            {
                normal_count++;
                sprintf(msg, "[TX] count=%lu  data=0x%02X\r\n",
                        (unsigned long)normal_count, data);
                UART_Print(msg);
            }
            else
            {
                UART_Print("[TX] timeout! check REQ/ACK/GND\r\n");
            }

            last_data = data;
            last_send_tick = HAL_GetTick();
        }

        /*
         * PA0 测速：
         * 按下后连续发送 1 秒，统计成功发送多少字节。
         */
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
        {
            HAL_Delay(30);

            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
            {
                UART_Print("\r\n[SPEED TEST] start, sending continuously for 1 second...\r\n");

                uint32_t success = 0;
                uint8_t test_data = 0;
                uint32_t t_start = HAL_GetTick();

                while ((HAL_GetTick() - t_start) < 1000)
                {
                    if (Parallel_Send(test_data++, 10))
                    {
                        success++;
                    }
                    else
                    {
                        UART_Print("[SPEED TEST] timeout during test\r\n");
                        break;
                    }
                }

                uint32_t elapsed_ms = HAL_GetTick() - t_start;
                uint32_t speed_Bps = 0;
                uint32_t speed_bps = 0;

                if (elapsed_ms > 0)
                {
                    speed_Bps = success * 1000UL / elapsed_ms;
                    speed_bps = speed_Bps * 8UL;
                }

                sprintf(msg,
                        "[SPEED TEST] bytes=%lu  time=%lu ms  speed=%lu B/s  (%lu bps)\r\n",
                        (unsigned long)success,
                        (unsigned long)elapsed_ms,
                        (unsigned long)speed_Bps,
                        (unsigned long)speed_bps);
                UART_Print(msg);

                UART_Print("[SPEED TEST] finished\r\n\r\n");

                while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
                {
                    /* 等待松手 */
                }
            }
        }

        HAL_Delay(10);
    }
}

/* 读取 PC0~PC3 拨码。
 * 上拉输入：拨到 ON 接地，读到 RESET，程序认为该位为 1。
 */
static uint8_t Read_Switch(void)
{
    uint8_t data = 0;

    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET) data |= 0x01;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET) data |= 0x02;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_RESET) data |= 0x04;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3) == GPIO_PIN_RESET) data |= 0x08;

    return data;
}

/* 一次性写 PD0~PD7，提高速度 */
static void Write_DataBus(uint8_t data)
{
    uint32_t odr = GPIOD->ODR;

    odr &= ~0x00FF;
    odr |= data;

    GPIOD->ODR = odr;
}

/* 查询方式发送 */
static uint8_t Parallel_Send_Polling(uint8_t data, uint32_t timeout_ms)
{
    uint32_t timeout;

    Write_DataBus(data);

    __NOP();
    __NOP();
    __NOP();

    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_SET);

    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(ACK_PORT, ACK_PIN) == GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - timeout) > timeout_ms)
        {
            HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);
            return 0;
        }
    }

    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);

    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(ACK_PORT, ACK_PIN) == GPIO_PIN_SET)
    {
        if ((HAL_GetTick() - timeout) > timeout_ms)
        {
            return 0;
        }
    }

    return 1;
}

/* 中断方式：启动一次发送 */
static uint8_t Parallel_Send_IT_Start(uint8_t data)
{
    if (tx_busy)
    {
        return 0;
    }

    Write_DataBus(data);

    __NOP();
    __NOP();
    __NOP();

    tx_busy = 1;
    tx_done = 0;
    tx_error = 0;
    tx_state = 1;
    tx_tick = HAL_GetTick();

    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_SET);

    return 1;
}

/* 中断方式：发送一次，并等待中断完成 */
static uint8_t Parallel_Send_IT_Blocking(uint8_t data, uint32_t timeout_ms)
{
    if (!Parallel_Send_IT_Start(data))
    {
        return 0;
    }

    while (!tx_done && !tx_error)
    {
        if ((HAL_GetTick() - tx_tick) > timeout_ms)
        {
            HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);

            tx_busy = 0;
            tx_done = 0;
            tx_error = 1;
            tx_state = 0;

            return 0;
        }
    }

    return tx_done ? 1 : 0;
}

/* 根据宏选择查询 / 中断 */
static uint8_t Parallel_Send(uint8_t data, uint32_t timeout_ms)
{
#if COMM_MODE == MODE_POLLING
    return Parallel_Send_Polling(data, timeout_ms);
#else
    return Parallel_Send_IT_Blocking(data, timeout_ms);
#endif
}

/* ACK 中断回调：发送端中断方式核心 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
#if COMM_MODE == MODE_INTERRUPT
    if (GPIO_Pin == ACK_PIN)
    {
        GPIO_PinState ack = HAL_GPIO_ReadPin(ACK_PORT, ACK_PIN);

        if ((tx_state == 1) && (ack == GPIO_PIN_SET))
        {
            /*
             * 接收端 ACK=1，说明已经读到数据。
             * 发送端拉低 REQ。
             */
            HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);
            tx_state = 2;
            tx_tick = HAL_GetTick();
        }
        else if ((tx_state == 2) && (ack == GPIO_PIN_RESET))
        {
            /*
             * 接收端 ACK=0，说明本次握手结束。
             */
            tx_busy = 0;
            tx_done = 1;
            tx_error = 0;
            tx_state = 0;
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
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PC0~PC3：拨码输入，上拉 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PA0：测速按键 */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PD0~PD7：数据总线输出 */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PG8：REQ 输出 */
    GPIO_InitStruct.Pin = REQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(REQ_PORT, &GPIO_InitStruct);

#if COMM_MODE == MODE_POLLING
    /* 查询方式：PG9 作为普通输入 */
    GPIO_InitStruct.Pin = ACK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(ACK_PORT, &GPIO_InitStruct);
#else
    /* 中断方式：PG9 作为 ACK 双边沿中断输入 */
    GPIO_InitStruct.Pin = ACK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(ACK_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif

    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);
    GPIOD->ODR &= ~0x00FF;
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