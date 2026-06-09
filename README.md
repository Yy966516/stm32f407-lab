# 微处理器实验 - 基于 STM32F407

课程实验代码，硬件平台：STM32F407 霸天虎V2

## 目录结构

```
stm32-lab/
├── lab1-IO-and-interrupt/
│   └── main.c                      # 实验1：IO 和中断（6学时）
├── lab2-segment-display-and-keys/  # 实验2：数码管和按键扩展（6学时）
│   └── main2_1_adder.c             # 单位加法器（PG9=等号中断, PG10=清零中断嵌套）
└── lab3-parallel-communication/    # 实验3：并行通信（12学时）
    ├── main_TX.c                   # 发送端（基础版，拨码变化触发握手）
    ├── main3_sender.c              # 发送端（含查询/中断模式切换 + 速度测试）
    ├── main3_receiver.c            # 接收端（含查询/中断模式切换 + 速度测试）
    ├── stm32f4xx_it.c              # 中断服务函数
    ├── stm32f4xx_hal_msp.c         # HAL MSP 初始化
    └── usart.c                     # UART 接收驱动
```

## 实验说明

### 实验1：IO 和中断
- GPIO 初始化、输入输出
- 中断向量表与启动代码分析
- 多中断优先级与抢占实验

### 实验2：数码管和按键扩展
- 面包板连接 LED、按键、拨码开关、数码管
- 实现单个十进制加法器（结果两位数码管显示）
- PG9（等号，优先级2）与 PG10（清零，优先级1）演示中断嵌套

### 实验3：并行通信
- 8 位并行数据总线（PD0~PD7）
- REQ/ACK 握手信号（PG8/PG9）
- 查询方式与中断方式可切换（`#define COMM_MODE`）
- PA0 按键触发 1 秒速度测试，串口输出 B/s 和 bps

## 工具链
- 编译：`make`
- 烧写：`make flash`（需配置 openOCD）
- 串口查看：SSCOM，115200 8N1
