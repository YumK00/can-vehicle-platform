# STM32F103 采集节点 A（collect-node）

> 平台的**现场层**之一：STM32F103C8T6 + FreeRTOS，负责红外/光敏两路模拟量的周期采集，
> 每 100 ms 向总线广播一帧 `0x101 COLLECT_SENSOR`，每秒发一帧 `0x701` 心跳，
> 同时在 OLED 上显示网关下发的网况与 A/B/网关的在线状态。
> 契约中该节点为 `NODE_A = 1`，`protocol.h` 里对应 `NODE_A`。

---

## 一、项目定位

| 项 | 内容 |
|---|---|
| 在链路中的角色 | 数据源：把两路 ADC 原始值变成 CAN 周期帧，交给网关转 MQTT |
| 上游 | 红外、光敏传感器（模拟量，接 ADC1 的 CH0/CH1） |
| 下游 | ESP32-S3 网关（[`../gateway`](../gateway)）；同一总线上还有执行节点 B（门锁节点不接 CAN） |
| 主要产出 | `0x101`（5 字节：`ir` + `ldr` + `seq`）、`0x701`（2 字节：魔数 `0xA5` + 计数） |
| 工程形态 | 完整 STM32CubeMX 工程：`11111.ioc`、`.mxproject`、`Drivers/`、`Middlewares/`、`CMakeLists.txt`、`CMakePresets.json`、`startup_stm32f103xb.s`、`STM32F103XX_FLASH.ld` |

采集与发送的完整数据通路（与 [`../../docs/architecture.md`](../../docs/architecture.md) 第 2 节一致）：

```text
传感器 → ADC1(CH0/CH1) → DMA1_Channel1 循环写入 adc_dma_buf[2]
   → photoAdcTask（100 ms，受 g_sample_enable 门控）
   → PROTO_PackASensor() 组 5 字节大端帧 → CAN_Send(0x101)
   → 总线 → 网关 pub_telemetry() → MQTT gw0805/nodeA/telemetry
```

---

## 二、目录结构

```text
firmware/collect-node/
├── 11111.ioc                        # CubeMX 工程文件（固件包 STM32Cube FW_F1 V1.8.7）
├── .mxproject                       # CubeMX 生成记录
├── CMakeLists.txt                   # 顶层构建：project 名 11111，追加 Core/Src 下的用户源文件
├── CMakePresets.json                # Debug / Release 两个预设（Ninja + gcc-arm-none-eabi 工具链）
├── cmake/
│   ├── gcc-arm-none-eabi.cmake      # 交叉工具链：-mcpu=cortex-m3、nano.specs、-T STM32F103XX_FLASH.ld
│   ├── starm-clang.cmake
│   └── stm32cubemx/CMakeLists.txt   # CubeMX 托管的 HAL / FreeRTOS / 启动文件清单
├── startup_stm32f103xb.s            # 启动文件（向量表 + Reset_Handler）
├── STM32F103XX_FLASH.ld             # 链接脚本：RAM 20K / FLASH 64K
├── .clangd                          # clangd 索引配置
├── .vscode/                         # c_cpp_properties / launch / settings
├── serial_logger.py                 # PC 侧串口采集脚本（115200，写 CSV，可打标签）
├── Core/
│   ├── Inc/                         # 各模块头文件（含 protocol.h 副本、FreeRTOSConfig.h）
│   └── Src/
│       ├── main.c                   # 时钟树、外设初始化顺序、Error_Handler
│       ├── freertos.c               # 5 个任务的创建与实现（本工程业务主体）
│       ├── can.c / can.h            # CAN1 250 kbps、接收中断、CAN_Send
│       ├── gpio.c / gpio.h          # MX_GPIO_Init、LED_Init
│       ├── adc_dma.c / adc_dma.h    # ADC1 双通道扫描 + DMA 循环
│       ├── iic.c / iic.h            # 软件 I2C（PB6/PB7，驱动 OLED）
│       ├── oled.c / oled.h          # SSD1306 显示
│       ├── serial.c / serial.h      # USART2 115200 8N1
│       ├── interrupt.c / interrupt.h # PB12 按键 EXTI + g_sample_enable
│       ├── stm32f1xx_hal_msp.c      # 各外设的引脚/时钟（CAN、USART2、ADC1）
│       ├── stm32f1xx_it.c           # TIM4 时基中断
│       ├── stm32f1xx_hal_timebase_tim.c
│       ├── system_stm32f1xx.c、syscalls.c、sysmem.c
│       └── （仅注释、仍参与编译）spi.c、spii.c、pwm_oc.c、pwm_ic.c、
│            timer_intern_interrupt.c、timer_extern_interrupt.c、adc.c、i2c.c
└── Middlewares/Third_Party/FreeRTOS/ # 内核源码 + CMSIS-RTOS2 封装（含 cmsis_os2.c）
```

---

## 三、核心模块

| 文件 | 职责 | 关键实现 |
|---|---|---|
| [`Core/Src/main.c`](Core/Src/main.c) | 启动流程 | `HAL_Init` → `SystemClock_Config` → `MX_GPIO_Init` → `OLED_Init` → `ADC_DMA_Init` → `Serial_Init` → `LED_Init` → `Key_Init` → `CAN_Init` → `osKernelInitialize` → `MX_FREERTOS_Init` → `osKernelStart` |
| [`Core/Src/freertos.c`](Core/Src/freertos.c) | 全部业务任务 | 用 CMSIS-RTOS2 的 `os*` 接口写（文件头有 `os*` ⇄ 原生 FreeRTOS 的对照表），建 5 个任务 + 2 个队列 |
| [`Core/Src/can.c`](Core/Src/can.c) | CAN1 驱动 | 位定时、过滤器、RX FIFO0 中断、`CAN_Send()`；中断回调里只把整帧拷进 `CanFrame` 再入队 |
| [`Core/Src/adc_dma.c`](Core/Src/adc_dma.c) | ADC 采集 | 扫描 + 连续 + 软件触发；规则组 CH0=RANK1（PA0）、CH1=RANK2（PA1），采样时间 55.5 周期；DMA1_Channel1 循环模式、半字搬运；`ADC_DMA_GetValue(ch)` 直接读 `adc_dma_buf[ch]` |
| [`Core/Src/iic.c`](Core/Src/iic.c) | 软件 I2C | `PB6 = SCL`、`PB7 = SDA`，用 DWT 的 `CYCCNT` 做微秒延时，避免空循环被优化掉 |
| [`Core/Src/oled.c`](Core/Src/oled.c) | OLED 显示 | 从机地址 `0x78`，`0x00` 写命令 / `0x40` 写数据；`OLED_ShowNum`、`OLED_ShowString`、`OLED_ShowHexNum` |
| [`Core/Src/serial.c`](Core/Src/serial.c) | 串口 | USART2 115200 8N1；提供 `Serial_SendData` / `Serial_SendString` / `Serial_Receive`，以及 `AA 55 len data sum` 自定义帧的收发 |
| [`Core/Src/interrupt.c`](Core/Src/interrupt.c) | 按键 | `PB12` 下降沿 EXTI，20 ms 软件消抖，翻转 `g_sample_enable`（0=停采，1=采集） |
| [`Core/Src/gpio.c`](Core/Src/gpio.c) | GPIO | `MX_GPIO_Init` 只开 GPIOC/GPIOD/GPIOA 时钟；`LED_Init` 自行补开 GPIOB，配 `PB15` 与 `PA8` 推挽输出并置高 |
| `Core/Inc/protocol.h` | 协议副本 | 与 `protocol/protocol.h` 逐字节一致，本工程编译的是 `CAN_ID_A_SENSOR`、`CAN_ID_A_HEARTBEAT`、`LEN_A_SENSOR`、`HB_MAGIC_A` 等常量 |

---

## 四、FreeRTOS 任务与中断

`configTICK_RATE_HZ = 1000`（1 tick = 1 ms）、`configMAX_PRIORITIES = 56`、`configTOTAL_HEAP_SIZE = 8192`、`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`。
下表优先级是 `osThreadAttr_t.priority` 的原值——`osThreadNew()` 直接透传，不做映射（`osPriorityAboveNormal = 32`、`osPriorityBelowNormal = 16`、`osPriorityNormal = 24`、`osPriorityLow7 = 15`、`osPriorityLow = 8`）。栈列按 `.stack_size` 的**字节**值给出（`xTaskCreate` 需除以 `sizeof(StackType_t) = 4` 得到字数）。

| 任务名 | 优先级 | 栈（字节） | 周期 / 阻塞点 | 职责 |
|---|---|---|---|---|
| `photoAdcTask` | 32（AboveNormal） | 512（`128 * 4`） | 100 ms（`osDelay(100)`） | 读 CH0/CH1 原始值，`PROTO_PackASensor()` 组帧，`CAN_Send(0x101, 5)`；`can_seq` 每帧 +1 |
| `serialSendTask` | 16（BelowNormal） | 1024（`256 * 4`） | 永久阻塞在 `osMessageQueueGet(xQueue, …, osWaitForever)` | 串口打印 `t=…,IR=…,LDR=…` + 刷新 OLED 前两行 |
| `canRxTask` | 24（Normal） | 1024（`256 * 4`） | 500 ms 超时轮询队列 | 处理 `0x000`/`0x702`/`0x703`/`0x103`，刷新 OLED 与 B/网关在线状态 |
| `canHeartTask` | 15（Low7） | 1024（`256 * 4`） | 1000 ms（`osDelay(1000)`） | 发 `0x701` 心跳：魔数 `HB_MAGIC_A = 0xA5` + 自增计数 |
| `defaultTask` | 8（Low） | 512（`128 * 4`） | 2000 ms | 自己把 `PC13` 配成推挽输出并翻转，做运行指示 |

队列：

| 名称 | 深度 | 元素 | 生产者 → 消费者 | 备注 |
|---|---|---|---|---|
| `canRxQueue` | 8 | `CanFrame`（`id` + `len` + `data[8]`） | CAN RX 中断 → `canRxTask` | 中断里 `osMessageQueuePut(..., 0U, 0U)`，超时 0，队列满就丢帧 |
| `xQueue` | 8 | `s_serialData`（`Infrared` + `Light`） | `photoAdcTask` → `serialSendTask` | 生产端 `osMessageQueuePut` 调用当前被注释，见第九节 |

信号量 / 互斥量：本工程**没有**创建任何信号量或互斥量；任务间只有上面两个队列，外加 `g_sample_enable`（`volatile uint8_t`，单字节读写原子）与两个 `volatile` 定时器标志位（`tim2_interrupt_flag`、`tim2_etr_overflow_flag`，定义在注释掉的文件里，因此未生效）。

中断：

| 中断源 | 优先级 | 处理内容 |
|---|---|---|
| `USB_LP_CAN1_RX0_IRQn` | 5（`HAL_CAN_MspInit` 中设置） | `HAL_CAN_IRQHandler` → `HAL_CAN_RxFifo0MsgPendingCallback`，取帧写入队列 |
| `EXTI15_10_IRQn`（`PB12`） | 5 | `HAL_GPIO_EXTI_Callback`，20 ms 消抖后翻转 `g_sample_enable`，用 `xTaskGetTickCountFromISR()` 取 tick |
| `TIM4_IRQn` | 15 | HAL 时基：`HAL_TIM_PeriodElapsedCallback` 里 `HAL_IncTick()` |
| `PendSV` / `SysTick` | 15 | FreeRTOS 上下文切换与节拍（`FreeRTOSConfig.h`） |

两个 5 号优先级都设在 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5` 上，`interrupt.c` 的注释解释了原因：中断里调用了 `FromISR` 类 API，优先级数值必须不小于该门限。

---

## 五、CAN 收发帧

总线参数（[`Core/Src/can.c`](Core/Src/can.c)）：

| 项 | 值 |
|---|---|
| 控制器 / 模式 | `CAN1`，`CAN_MODE_NORMAL` |
| Prescaler | 9（APB1 36 MHz / 9 = 4 MHz 的 tq 时钟） |
| BS1 / BS2 / SJW | `CAN_BS1_13TQ` / `CAN_BS2_2TQ` / `CAN_SJW_2TQ` |
| 位时间 | `(1 + 13 + 2) = 16 tq` → 4 MHz / 16 = **250 kbps** |
| 采样点 | `(1 + 13) / 16 = **87.5%**` |
| ABOM / NART | 均 `ENABLE`（bus-off 自动恢复、失败硬件自动重发） |
| 接收 FIFO | FIFO0，`ReceiveFifoLocked = DISABLE`（满时新帧覆盖旧帧） |
| 过滤器 | 掩码全 0 = 接收全部 ID（`CAN_FILTERMODE_IDMASK` + `CAN_FILTERSCALE_32BIT`） |

本工程发出的帧：

| ID | DLC | 方向 | 名称 | 字段 | 触发 |
|---|---|---|---|---|---|
| `0x101` | 5 | A → 广播 | COLLECT_SENSOR | `ir`(2，大端) + `ldr`(2，大端) + `seq`(1) | `photoAdcTask`，100 ms；**仅当 `g_sample_enable == 1`** |
| `0x701` | 2 | A → 广播 | COLLECT_HEARTBEAT | `magic = 0xA5` + `cnt`(1，自增) | `canHeartTask`，1000 ms，无条件发送 |

本工程接收并处理的帧：

| ID | DLC | 处理 |
|---|---|---|
| `0x000` | 1 | `frame.data[0] == ESTOP_ACTION_CLEAR(2)` 则清掉 OLED 上的 `ESTOP`，其他任何值都显示 `ESTOP`（Fail-Safe） |
| `0x702` | 2 | B 心跳：ID + DLC + 魔数 `0x5A` 三对才算有效，刷新 `last_b_tick`，OLED 第 4 行显示 `b ON` / `b OFF` |
| `0x703` | 2 | 网关心跳：ID + DLC + 魔数 `0xC3`，OLED 显示 `gw ON` / `gw OFF` |
| `0x103` | 4 | 网关状态：OLED 第 3 行依次显示 `wifi`、`mqtt`、`cache`（`PROTO_GetU16(frame.data + 2)`） |

判活门限 `HB_TIMEOUT_MS = 3000`：超过 3 秒没有有效心跳就置 `b OFF` / `gw OFF`，且每个状态只在变化的那一次刷屏。

---

## 六、硬件与引脚

MCU：**STM32F103C8T6**（LQFP48，Flash 64 KB / RAM 20 KB，见 `11111.ioc` 的 `Mcu.CPN` 与链接脚本）。

时钟树（`SystemClock_Config()`）：

| 项 | 值 | 说明 |
|---|---|---|
| HSE | 8 MHz 外部晶振 | `RCC_HSE_ON`，`HSEPredivValue = RCC_HSE_PREDIV_DIV1` |
| PLL | `RCC_PLL_MUL9`，源为 HSE | 8 MHz × 9 = 72 MHz |
| SYSCLK | 72 MHz | `RCC_SYSCLKSOURCE_PLLCLK` |
| AHB 分频 | `RCC_SYSCLK_DIV2` | **HCLK = 36 MHz**，这也是 `can.c` 里"APB1 = 36 MHz"的来源 |
| APB1 / APB2 分频 | `RCC_HCLK_DIV1` | PCLK1 = PCLK2 = 36 MHz |
| Flash 等待周期 | `FLASH_LATENCY_2` | |
| ADCCLK | `RCC_ADCPCLK2_DIV6` | 在 `HAL_ADC_MspInit` 里设置 |

引脚：

| 引脚 | 功能 | 配置位置 / 说明 |
|---|---|---|
| `PA0` | ADC1_IN0（红外） | `HAL_ADC_MspInit`，`GPIO_MODE_ANALOG` |
| `PA1` | ADC1_IN1（光敏） | 同上 |
| `PA2` | USART2_TX | `HAL_UART_MspInit`，`AF_PP`，115200 8N1 |
| `PA3` | USART2_RX | `HAL_UART_MspInit`，输入 + 上拉 |
| `PA8` | 推挽输出（`LED_Init` 置高） | `LED_Init`；也是 `Error_Handler` 里翻转的引脚 |
| `PA11` | CAN1_RX | `HAL_CAN_MspInit`，输入 |
| `PA12` | CAN1_TX | `HAL_CAN_MspInit`，`AF_PP` |
| `PA13` / `PA14` | SWDIO / SWCLK | `HAL_MspInit` 里 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` |
| `PB6` / `PB7` | 软件 I2C：SCL / SDA（OLED 0x78） | `iic.c` 宏定义 |
| `PB12` | 按键，下降沿 EXTI | `Key_Init`，内部上拉 |
| `PB15` | 推挽输出（LED） | `LED_Init`，初始置高 |
| `PC13` | 推挽输出（默认任务闪烁） | `StartDefaultTask` 内自行 `HAL_GPIO_Init` |
| `PD0` / `PD1` | 外部晶振输入（HSE） | `main.c` 的 `RCC_OscInitStruct` 使能 |

CAN 物理层：必须外接收发器（TJA1050 等）接 `PA11`/`PA12`，总线两端各一个 120 Ω 终端电阻，不接则总线持续报错、收不到帧。

---

## 七、构建与烧录

环境：`arm-none-eabi-gcc`（`cmake/gcc-arm-none-eabi.cmake` 里以 `arm-none-eabi-` 前缀调用 gcc/g++/objcopy/size）、CMake ≥ 3.22、Ninja。

```bash
# CMake 路线（预设已配好 Ninja + 交叉工具链，产物在 build/Debug）
cd firmware/collect-node
cmake --preset Debug
cmake --build build/Debug
# 产物：build/Debug/11111.elf / .hex / .bin
```

```bash
# 烧录（ST-Link，按实际探针与端口调整）
STM32_Programmer_CLI -c port=SWD -w build/Debug/11111.bin 0x08000000 -rst
```

也可以在 STM32CubeIDE 中直接打开 `11111.ioc` 对应的工程 Build & Flash。编译关键设置：`-mcpu=cortex-m3`、`--specs=nano.specs`、`-Wl,--gc-sections`、`-T STM32F103XX_FLASH.ld`、宏 `USE_HAL_DRIVER` + `STM32F103xB`；Debug 用 `-O0 -g3`，Release 用 `-Os -g0`。

串口侧可选脚本：`python serial_logger.py -p COM3 -b 115200 -o data.csv`（默认 115200，与 `Serial_Init()` 一致；需要 `pip install pyserial`）。注意 `serialSendTask` 目前收不到数据，见第九节。

---

## 八、与其他组件的关系

| 对象 | 关系 |
|---|---|
| [`../../README.md`](../../README.md) | 平台总览与构建总说明 |
| [`../gateway/README.md`](../gateway/README.md) | 网关：把本节点的 `0x101` 转成 `gw0805/nodeA/telemetry`，并用 `0x703` + `0x103` 反向告知网况 |
| [`../control-node/README.md`](../control-node/README.md) | 执行节点 B（`NODE_B = 2`），本工程只订阅它的心跳 `0x702` |
| [`../door-node/README.md`](../door-node/README.md) | 门锁节点（ESP-NOW，独立扩展，不接 CAN） |
| [`../../protocol/protocol.h`](../../protocol/protocol.h) | 协议真源；本工程副本在 `Core/Inc/protocol.h`，由 `tools/sync_contract_copies.py` 同步 |
| [`../../protocol/generated/contract.md`](../../protocol/generated/contract.md) | 帧表、MQTT 主题、命令映射的可读版本 |
| [`../../protocol/README.md`](../../protocol/README.md) | 契约生成与校验流程 |
| [`../../tools/test_contract.py`](../../tools/test_contract.py) | 主机侧协议自测（无需硬件） |
| [`../../host/desktop-console`](../../host/desktop-console) | 遥测的最终消费端 |
| [`../../docs/architecture.md`](../../docs/architecture.md) | 分层视图与数据流 |
| [`../../docs/troubleshooting/03-CAN故障排查-A节点能收不能发.md`](../../docs/troubleshooting/03-CAN故障排查-A节点能收不能发.md) | 与本节点发送链路相关的排查实录 |

改协议时本工程只需要重新同步副本，不要手改 `Core/Inc/protocol.h`：

```bash
python tools/gen_contract.py            # 若改了 contract.json 中的帧/主题
python tools/sync_contract_copies.py    # 把 protocol/protocol.h 同步到本工程
python tools/test_contract.py           # 主机侧自测
```

---

## 九、已知限制与注意事项

以下都是代码里能直接读到的现状：

1. **串口与 OLED 前两行实际不更新。** `photoAdcTask` 里往 `xQueue` 投递数据的 `osMessageQueuePut(xQueue, &SerialData, 0U, 0U)` 被注释掉了，而 `serialSendTask` 又在 `osWaitForever` 上永久阻塞。结果是：`Serial_SendData()` 与 `serialSendTask` 里那两条 `OLED_ShowNum(1, …)` / `OLED_ShowNum(2, …)` 都不会被触发，OLED 第 1、2 行的传感器值不会刷新（第 3、4 行的网况与在线状态由 `canRxTask` 正常刷新）。
2. **8 个源文件整体是注释。** `spi.c`、`spii.c`、`pwm_oc.c`、`pwm_ic.c`、`timer_intern_interrupt.c`、`timer_extern_interrupt.c`、`adc.c`、`i2c.c` 的内容**全部**是 `//` 注释（无一行有效代码），但仍出现在 `CMakeLists.txt` 的 `target_sources` 里，会编成空的目标文件。另有 `DHT22.c` 与 `oneWire.c`（单总线，`PC15`）存在但不在任何源文件清单中，不参与构建。
3. **采集默认是关的。** `g_sample_enable` 初值为 0，`photoAdcTask` 只有在它为 1 时才采样并发送 `0x101`。要让它发数据，必须按一下 `PB12` 按键（或改初值）。
4. **本节点不处理契约里发给 A 的下行帧。** `canRxTask` 只认 `0x000` / `0x702` / `0x703` / `0x103`；`contract.md` 中 `0x301 CTRL_MOTOR_COLLECT` 与 `0x311 CTRL_TARGET_COLLECT` 在代码里没有对应分支，收到也只会被丢弃。本节点也没有发送 `0x011 ALARM_COLLECT` 的代码。
5. **软件 I2C 的延时基数与实际主频不一致。** `iic.c` 定义 `CPU_FREQ_MHZ = 72`，按"1 µs = 72 个 DWT 周期"换算；而本工程做了 `AHB ÷2`，HCLK = 36 MHz，DWT 的 `CYCCNT` 按内核时钟计数。因此 `Delay_us()` 的实际延时约为标称值的两倍（软件 I2C 只会更慢，不会更快）。`pwm_oc.c`、`pwm_ic.c`、`timer_*` 等注释文件里"APB2 = 72 MHz""TIM2 时钟 72 MHz"的推算也基于 72 MHz 假设。
6. **ADC 时钟注释与代码不一致。** `adc_dma.c` / `adc.c` 的注释写"APB2 = 72 MHz，ADCPRE 分频 6 → ADCCLK = 12 MHz"，但 `SystemClock_Config()` 是 AHB ÷2，PCLK2 = 36 MHz，代码里实际设置的是 `RCC_ADCPCLK2_DIV6`；`11111.ioc` 记录的 `RCC.ADCFreqValue` 又是 18 MHz。三处数值不一致，实际 ADCCLK 以时钟配置与分频寄存器为准。
7. **`11111.ioc` 只登记了 SWD 与 LSE 引脚。** 文件内 `Mcu.Pin0…Pin7` 只包含 `PC14/PC15`（LSE）、`PD0/PD1`（HSE）、`PA13/PA14`（SWD）与两个虚拟引脚；CAN、USART2、ADC、I2C、按键、LED 的引脚全部写在 `stm32f1xx_hal_msp.c` 与各驱动 `.c` 里。用 CubeMX 重新生成代码可能会把这些手写配置覆盖掉。
8. **`Error_Handler()` 先关中断再 `HAL_Delay()`。** 函数体是 `__disable_irq()` 后 `while (1) { HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_8); HAL_Delay(200); }`，而 `HAL_Delay` 依赖 SysTick 中断推进 `uwTick`；进入该路径后无法按预期闪灯，实际是停机等待复位。
9. **堆只有 8 KB。** `configTOTAL_HEAP_SIZE = 8192`，5 个任务（栈合计约 4 KB）加 TCB 与两个队列都从这里分配；`MX_FREERTOS_Init()` 里对每个 `osThreadNew` 的返回值都做了 NULL 检查并在失败时进 `Error_Handler()`（代码注释解释了为什么必须逐个查）。
10. **构建产物名是 `11111.*`。** `CMakeLists.txt` 的 `CMAKE_PROJECT_NAME` 与 `11111.ioc` 一致，所以输出是 `11111.elf` / `11111.hex` / `11111.bin`，与目录名 `collect-node` 不同；写烧录脚本时按 `11111` 找文件，不要按目录名推断。
