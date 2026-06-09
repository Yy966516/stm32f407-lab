#include "stm32f4xx_hal.h"

/*
 * 实验3 并行通信 - 发送端
 *
 * PC0~PC3：拨码开关输入，拨到 ON 时接地，程序认为该位为 1
 * PD0~PD7：8 位并行数据输出
 * PG8：REQ 输出，通知接收方“数据有效”
 * PG9：ACK 输入，等待接收方“已经读完”
 */

#define REQ_PORT GPIOG
#define REQ_PIN  GPIO_PIN_8

#define ACK_PORT GPIOG
#define ACK_PIN  GPIO_PIN_9

static void GPIO_Init_All(void);
static uint8_t Read_Switch(void);
static void Write_DataBus(uint8_t data);
static void Parallel_Send(uint8_t data);

int main(void)
{
    HAL_Init();
    GPIO_Init_All();

    uint8_t last_data = 0xFF;

    while (1)
    {
        uint8_t data = Read_Switch();

        /*
         * 拨码变化后才发送。
         * 第一次 last_data = 0xFF，所以会先发送一次当前状态。
         */
        if (data != last_data)
        {
            Parallel_Send(data);
            last_data = data;
        }

        HAL_Delay(20);
    }
}

/* 读取 PC0~PC3 拨码开关 */
static uint8_t Read_Switch(void)
{
    uint8_t data = 0;

    /*
     * 因为 PC0~PC3 使用上拉输入：
     * 不拨 / 断开 = 高电平 = 0
     * 拨到 ON / 接地 = 低电平 = 1
     */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET) data |= 0x01;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET) data |= 0x02;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_RESET) data |= 0x04;
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3) == GPIO_PIN_RESET) data |= 0x08;

    return data;
}

/* 把一个字节写到 PD0~PD7 */
static void Write_DataBus(uint8_t data)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, (data & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, (data & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, (data & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, (data & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, (data & 0x20) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, (data & 0x40) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, (data & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* 一次完整的握手发送 */
static void Parallel_Send(uint8_t data)
{
    uint32_t timeout;

    /* 1. 先把数据放到总线上 */
    Write_DataBus(data);

    /* 稍微等待，让数据线稳定 */
    HAL_Delay(1);

    /* 2. 拉高 REQ，告诉接收方：数据有效 */
    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_SET);

    /* 3. 等待接收方 ACK 拉高 */
    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(ACK_PORT, ACK_PIN) == GPIO_PIN_RESET)
    {
        if (HAL_GetTick() - timeout > 1000)
        {
            /* 超时，说明接线或接收端有问题，放弃本次发送 */
            HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);
            return;
        }
    }

    /* 4. 收到 ACK 后，拉低 REQ */
    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);

    /* 5. 等待接收方 ACK 也拉低，准备下一次 */
    timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(ACK_PORT, ACK_PIN) == GPIO_PIN_SET)
    {
        if (HAL_GetTick() - timeout > 1000)
        {
            return;
        }
    }
}

static void GPIO_Init_All(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PC0~PC3：拨码输入，上拉 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PD0~PD7：数据输出 */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PG8：REQ 输出 */
    GPIO_InitStruct.Pin = REQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(REQ_PORT, &GPIO_InitStruct);

    /* PG9：ACK 输入，下拉 */
    GPIO_InitStruct.Pin = ACK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(ACK_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(REQ_PORT, REQ_PIN, GPIO_PIN_RESET);
}