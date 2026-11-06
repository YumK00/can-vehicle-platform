# STM32F103 执行节点 B（control-node）

> 平台的**现场层**之一：STM32F103C8T6 + FreeRTOS，接收网关下发的电机命令帧 `0x302`，
> 用双 H 桥方向脚 + TIM3 PWM 驱动一路直流电机，每秒回读一帧 `0x102 CONTROL_STATUS`，
> 并在网关心跳超时或收到广播急停时自动停车。
> 契约中该节点为 `NODE_B = 2`，`protocol.h` 里对应 `NODE_B`。

---

## 一、项目定位

| 项 | 内容 |
|---|---|
| 在链路中的角色 | 执行器：把"云端命令 → MQTT → CAN"最终落成电机转向与占空比，并回读真实状态 |
| 输入 | `0x302`（网关下发电机命令）、`0x000`（广播急停）、`0x703`（网关心跳，兼电机保活） |
| 输出 | `0x102`（1 Hz 状态回读）、`0x702`（1 Hz 心跳） |
| 安全兜底 | 网关心跳超时 3 秒 → 停车；急停期间丢弃积压命令，解除后必须重新下发 |
| 工程形态 | 完整 STM32CubeMX 工程：`22222.ioc`、`.mxproject`、`Drivers/`、`Middlewares/`、`CMakeLists.txt`、`CMakePresets.json`、`startup_stm32f103xb.s`、`STM32F103XX_FLASH.ld` |

闭环在"执行端"这一段的完整通路：

```text
MQTT gw0805/nodeB/cmd/motor {"1,50"}（网关侧负载为 "1,50"）
   → 网关 can_bridge_mqtt() 查表得 GRP_CTRL_MOTOR | NODE_B → 0x302
   → 本节点 canRxTask：PROTO_UnpackCtrlMotor() → cid 幂等判断 → 写命令邮箱
   → motorTask（10 ms 周期，优先级高于 canRxTask）：Motor_SetSpeed() 换向 + 占空比
   → 1 秒后 canTxTask 发 0x102 回读真实状态 → 网关 → MQTT → 上位机确认
```

---

## 二、目录结构

```text
firmware/control-node/
├── 22222.ioc                        # CubeMX 工程文件（固件包 STM32Cube FW_F1 V1.8.7）
├── .mxproject                       # CubeMX 生成记录
├── CMakeLists.txt                   # 顶层构建（注意 CMAKE_PROJECT_NAME 仍是 11111）
├── CMakePresets.json                # Debug / Release 预设（Ninja + gcc-arm-none-eabi）
├── cmake/
│   ├── gcc-arm-none-eabi.cmake      # 交叉工具链：-mcpu=cortex-m3、nano.specs、-T STM32F103XX_FLASH.ld
│   ├── starm-clang.cmake
│   └── stm32cubemx/CMakeLists.txt   # CubeMX 托管的 HAL / FreeRTOS / 启动文件清单
├── startup_stm32f103xb.s            # 启动文件
├── STM32F103XX_FLASH.ld             # 链接脚本：RAM 20K / FLASH 64K
├── .clangd                          # clangd 索引配置
├── .vscode/                         # c_cpp_properties / launch / settings
├── serial_logger.py                 # PC 侧串口采集脚本（与采集节点同名同版本）
├── Core/
│   ├── Inc/                         # 头文件（含 protocol.h 副本、FreeRTOSConfig.h、motor.h、pwm.h）
│   └── Src/
│       ├── main.c                   # 时钟树、外设初始化顺序、Error_Handler
│       ├── freertos.c               # 5 个任务 + 命令邮箱 + 判活/急停逻辑（本工程业务主体）
│       ├── can.c / can.h            # CAN1 250 kbps、接收中断、CAN_Send
│       ├── motor.c / motor.h        # 方向脚 + 换向停顿 + 限幅，Motor_SetSpeed/GetSpeed
│       ├── pwm.c / pwm.h            # TIM3_CH1 单路 PWM，SetPWMDuty/GetDuty/WaitNextPeriod
│       ├── gpio.c / gpio.h          # MX_GPIO_Init、LED_Init（PB15 / PA8 / PC13）
│       ├── adc_dma.c / adc_dma.h    # ADC1 双通道 + DMA（已初始化，本工程无消费者，见第九节）
│       ├── iic.c / iic.h            # 软件 I2C（PB8/PB9，驱动 OLED）
│       ├── oled.c / oled.h          # SSD1306 显示
│       ├── serial.c / serial.h      # USART2 115200 8N1
│       ├── interrupt.c / interrupt.h # PB12 按键 EXTI + g_sample_enable
│       ├── stm32f1xx_hal_msp.c      # CAN / USART2 / ADC1 的引脚与时钟
│       ├── stm32f1xx_it.c           # TIM4 时基中断
│       ├── stm32f1xx_hal_timebase_tim.c
│       └── system_stm32f1xx.c、syscalls.c、sysmem.c
└── Middlewares/Third_Party/FreeRTOS/ # 内核源码 + CMSIS-RTOS2 封装
```

与采集节点相比，本工程没有 `DHT22`、`oneWire`、`spi/spii`、`pwm_ic/pwm_oc`、`timer_*_interrupt` 这些文件，`CMakeLists.txt` 的追加源文件清单也只列了 `iic.c / oled.c / interrupt.c / adc_dma.c / serial.c / can.c / pwm.c / motor.c` 八个。

---

## 三、核心模块

| 文件 | 职责 | 关键实现 |
|---|---|---|
| [`Core/Src/freertos.c`](Core/Src/freertos.c) | 全部业务逻辑 | 5 个任务、`g_motor_target` / `g_motor_cmd_cnt` 命令邮箱、"网关在线"唯一判据 `B_GwOnline()`、`B_GetFlags()` 现算状态位 |
| [`Core/Src/motor.c`](Core/Src/motor.c) | 电机执行层 | 方向脚 `PA4`/`PA5` 组合出停止/正转/反转；`Motor_SetSpeed(-100…+100)` 限幅；**换向前先 `SetPWMDuty(0)` 再 `PWM_WaitNextPeriod()`**，等 0% 真正落到引脚后才翻方向 |
| [`Core/Src/pwm.c`](Core/Src/pwm.c) | PWM 通道 | `PA6 = TIM3_CH1`，`Prescaler = 71`、`Period = 1000 - 1`、PWM1 模式、高电平有效、打开 CCR 预装载；`PWM_GetDuty()` 直接读 CCR 反算，不另存状态 |
| [`Core/Src/can.c`](Core/Src/can.c) | CAN1 驱动 | 与采集节点参数完全一致；RX FIFO0 中断里整帧入队（超时 0），不解析 |
| [`Core/Src/main.c`](Core/Src/main.c) | 启动流程 | `HAL_Init` → `SystemClock_Config` → `MX_GPIO_Init` → `OLED_Init` → `ADC_DMA_Init` → `Serial_Init` → `LED_Init` → `Key_Init` → `Motor_Init` → `CAN_Init` → `osKernelInitialize` → `MX_FREERTOS_Init` → `osKernelStart` |
| [`Core/Src/gpio.c`](Core/Src/gpio.c) | GPIO | `MX_GPIO_Init` 只开 GPIOC/GPIOD/GPIOA；`LED_Init` 自行补开 GPIOB，配 `PB15`、`PA8`、`PC13` 三路推挽输出并置高 |
| [`Core/Src/iic.c`](Core/Src/iic.c) | 软件 I2C | **`PB8 = SCL`、`PB9 = SDA`**（与采集节点的 PB6/PB7 不同），DWT `CYCCNT` 延时，驱动地址 `0x78` 的 OLED |
| `Core/Inc/protocol.h` | 协议副本 | 与 `protocol/protocol.h` 逐字节一致；本工程用到 `CAN_ID_B_STATUS`、`CAN_ID_CTRL_MOTOR_B`、`HB_MAGIC_B`、`ST_COMM_OK`、`ST_ESTOP`、`MODE_MANUAL` 等 |

---

## 四、FreeRTOS 任务与中断

`configTICK_RATE_HZ = 1000`（1 tick = 1 ms）、`configMAX_PRIORITIES = 56`、`configTOTAL_HEAP_SIZE = 8192`、`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`。
优先级是 `osThreadAttr_t.priority` 原值（`osThreadNew()` 直接透传）；栈按 `.stack_size` 的字节值给出。

| 任务名 | 优先级 | 栈（字节） | 周期 / 阻塞点 | 职责 |
|---|---|---|---|---|
| `motorTask` | 32（AboveNormal） | 512（`128 * 4`） | 10 ms（`MOTOR_TASK_PERIOD_MS`） | 唯一调用 `Motor_SetSpeed()` 的任务：消费命令邮箱、执行急停、做网关心跳断线保护 |
| `canTxTask` | 32（AboveNormal） | 1024（`256 * 4`） | 1000 ms | 取一次 `Motor_GetSpeed()` 快照，组 `0x102` 上报（`motor`/`pwm`/`mode`/`flags`） |
| `canRxTask` | 24（Normal） | 1024（`256 * 4`） | 500 ms 超时轮询队列 | 刷新 OLED，处理 `0x000`/`0x101`/`0x701`/`0x703`/`0x302`/`0x103`，写命令邮箱 |
| `canHeartTask` | 15（Low7） | 1024（`256 * 4`） | 1000 ms | 发 `0x702`：魔数 `HB_MAGIC_B = 0x5A` + 自增计数 |
| `defaultTask` | 8（Low） | 512（`128 * 4`） | 2000 ms | 翻转 `PC13` 做运行指示 |

**单写者邮箱**（`canRxTask` 写、`motorTask` 读）：

| 变量 | 类型 | 写入顺序 / 语义 |
|---|---|---|
| `g_motor_target` | `volatile int16_t` | 目标速度 `-100…+100`，符号即方向 |
| `g_motor_cmd_cnt` | `volatile uint8_t` | 命令序号，每来一条新命令 +1（自然回绕）；**先写 target 再写 cnt**，读到新 cnt 者必然看到新 target |
| `g_estop_istrue` | `volatile bool` | `0x000` 且动作不是 `ESTOP_ACTION_CLEAR` 时置 `true` |
| `g_gw_hb_tick` | `volatile uint32_t` | 最近一次有效 `0x703` 的 tick；为 0 表示"从没见过网关"，此时 `B_GwOnline()` 返回 false |

队列与同步设施：

| 名称 | 规格 | 说明 |
|---|---|---|
| `canRxQueue` | 深度 8，元素 `CanFrame` | CAN RX 中断 → `canRxTask`；`osMessageQueuePut(..., 0U, 0U)` 不阻塞 |
| 信号量 / 互斥量 | 无 | 本工程没有创建任何信号量或互斥量，完全靠邮箱变量 + 队列；`Motor_SetSpeed()` 靠"唯一调用者"保证互斥（`motorTask`） |

中断：

| 中断源 | 优先级 | 处理内容 |
|---|---|---|
| `USB_LP_CAN1_RX0_IRQn` | 5 | `HAL_CAN_RxFifo0MsgPendingCallback` 取帧入队 |
| `EXTI15_10_IRQn`（`PB12`） | 5 | 20 ms 消抖后翻转 `g_sample_enable`（用 `xTaskGetTickCountFromISR()`） |
| `TIM4_IRQn` | 15 | HAL 时基 `HAL_IncTick()` |
| `PendSV` / `SysTick` | 15 | FreeRTOS 上下文切换与节拍 |

关键常量（[`Core/Src/freertos.c`](Core/Src/freertos.c) 头部）：`HB_TIMEOUT_MS = 3000`、`MOTOR_GW_LOST_MS = 3000`、`MOTOR_TASK_PERIOD_MS = 10`、`MOTOR_START_SPEED = 20`（方向命令不带速度，静止起步时用 20%）。

---

## 五、CAN 收发帧

总线参数与采集节点完全相同（两处 `can.c` 一字不差）：`CAN1` + `CAN_MODE_NORMAL`、`Prescaler = 9`、`BS1 = 13TQ`、`BS2 = 2TQ`、`SJW = 2TQ` → 16 tq、**250 kbps**、采样点 **87.5%**；`ABOM` 与 `NART` 均 `ENABLE`；接收 FIFO0，掩码全 0 接收全部 ID。

本工程发出的帧：

| ID | DLC | 方向 | 名称 | 字段 | 触发 |
|---|---|---|---|---|---|
| `0x102` | 4 | B → 广播 | CONTROL_STATUS | `motor`(1，`MST_xxx`) + `pwm`(1，0~100) + `mode`(1，`MODE_xxx`) + `flags`(1，`ST_xxx` 位或) | `canTxTask`，1000 ms |
| `0x702` | 2 | B → 广播 | CONTROL_HEARTBEAT | `magic = 0x5A` + `cnt`(1，自增) | `canHeartTask`，1000 ms |

`0x102` 四个字段的实际来源：

| 字段 | 取值 | 说明 |
|---|---|---|
| `motor` | `Motor_GetSpeed() > 0` → `MST_FWD`；`< 0` → `MST_REV`；`== 0` → `MST_IDLE` | 是"回读"当前执行状态，不是命令 |
| `pwm` | `Motor_GetSpeed()` 的绝对值 | 对齐协议里 `B1 = 0~100 百分比` |
| `mode` | 固定 `MODE_MANUAL` | 代码注释标明是占位，`0x332` 模式切换未接 |
| `flags` | `B_GetFlags()` 每次现算 | `bit0 ST_COMM_OK` = 3 秒内收到过网关心跳；`bit3 ST_ESTOP` = 急停中；`bit1 ST_THRESH` / `bit2 ST_ALARM` 没有生产者，恒为 0 |

本工程接收并处理的帧：

| ID | DLC | 处理 |
|---|---|---|
| `0x302` | 3 | `PROTO_UnpackCtrlMotor()`；`cid != last_cid` 才执行（幂等）。`MOTOR_STOP` → 0；`MOTOR_FWD`/`MOTOR_REV` → 保持当前速度大小（静止时用 `MOTOR_START_SPEED = 20`）只换方向；其余按 `MOTOR_SETSPEED`，速度取 `arg`、方向沿用当前符号 |
| `0x000` | 1 | `ESTOP_ACTION_CLEAR` → `g_estop_istrue = false`；其他任何值 → `true` 并显示 `ESTOP`；急停期间 `motorTask` 持续 `Motor_SetSpeed(0)` 并吃掉积压命令 |
| `0x101` | 5 | `PROTO_UnpackASensor()`，OLED 第 2 行显示 `ir`/`ldr`/`seq`，并用 `PROTO_SeqLost()` 在第 1 行显示丢帧数（首帧显示 `--`） |
| `0x701` | 2 | A 心跳：ID + DLC + 魔数 `0xA5`，OLED 显示 `a ON` / `a OFF` |
| `0x703` | 2 | 网关心跳：ID + DLC + 魔数 `0xC3`；同时刷新局部 `last_gw_tick`（OLED 用）与全局 `g_gw_hb_tick`（电机保活用） |
| `0x103` | 4 | 网关状态：OLED 第 3 行显示 `wifi`、`mqtt`、`cache` |

---

## 六、硬件与引脚

MCU：**STM32F103C8T6**（LQFP48，Flash 64 KB / RAM 20 KB，见 `22222.ioc` 的 `Mcu.CPN` 与链接脚本）。

时钟树（`SystemClock_Config()`，与采集节点一致）：

| 项 | 值 | 说明 |
|---|---|---|
| HSE | 8 MHz 外部晶振 | `RCC_HSE_ON`，`HSEPredivValue = DIV1` |
| PLL | `RCC_PLL_MUL9`（源 HSE） | 8 MHz × 9 = 72 MHz |
| SYSCLK | 72 MHz | `RCC_SYSCLKSOURCE_PLLCLK` |
| AHB 分频 | `RCC_SYSCLK_DIV2` | **HCLK = 36 MHz** |
| APB1 / APB2 分频 | `RCC_HCLK_DIV1` | PCLK1 = PCLK2 = 36 MHz |
| Flash 等待周期 | `FLASH_LATENCY_2` | |
| ADCCLK | `RCC_ADCPCLK2_DIV6` | `HAL_ADC_MspInit` 中设置（本工程 ADC 无消费者） |

引脚：

| 引脚 | 功能 | 配置位置 / 说明 |
|---|---|---|
| `PA0` / `PA1` | ADC1_IN0 / ADC1_IN1 | `HAL_ADC_MspInit`，模拟输入；本工程未读取 |
| `PA2` / `PA3` | USART2_TX / USART2_RX | `HAL_UART_MspInit`，115200 8N1 |
| `PA4` | H 桥 `IN1`（`MOTOR_PIN1`） | `Motor_Init`，推挽输出 |
| `PA5` | H 桥 `IN2`（`MOTOR_PIN2`） | `Motor_Init`，推挽输出 |
| `PA6` | TIM3_CH1 PWM（`PWM_GPIO_PIN`） | `PWM_Init`，`AF_PP` |
| `PA8` | 推挽输出（`LED_Init` 置高） | `LED_Init`；也是 `Error_Handler` 翻转的引脚 |
| `PA11` / `PA12` | CAN1_RX / CAN1_TX | `HAL_CAN_MspInit` |
| `PA13` / `PA14` | SWDIO / SWCLK | `HAL_MspInit` 里 `SWJ_NOJTAG()` |
| `PB8` / `PB9` | 软件 I2C：SCL / SDA（OLED 0x78） | `iic.c` 宏定义 |
| `PB12` | 按键，下降沿 EXTI | `Key_Init`，内部上拉 |
| `PB15` | 推挽输出（LED） | `LED_Init`，初始置高 |
| `PC13` | 推挽输出（默认任务闪烁） | `LED_Init` 配好，`StartDefaultTask` 翻转 |
| `PD0` / `PD1` | HSE 晶振输入 | `RCC_OscInitStruct` 使能 |

电机驱动接线（`motor.c` 的注释给出真值表）：

| `IN1` | `IN2` | 结果 |
|---|---|---|
| 0 | 0 | 停（滑行，电机两端悬空） |
| 1 | 0 | 正转 |
| 0 | 1 | 反转 |
| 1 | 1 | 刹车（绕组短路，本工程不用） |

CAN 物理层同样必须外接收发器接 `PA11`/`PA12`，总线两端各一个 120 Ω 终端电阻。

---

## 七、构建与烧录

环境：`arm-none-eabi-gcc`（`cmake/gcc-arm-none-eabi.cmake`）、CMake ≥ 3.22、Ninja。

```bash
# CMake 路线（预设已配好 Ninja + 交叉工具链）
cd firmware/control-node
cmake --preset Debug
cmake --build build/Debug
# 产物：build/Debug/11111.elf / .hex / .bin（工程名沿用 11111，见第九节）
```

```bash
# 烧录（ST-Link，按实际探针与端口调整）
STM32_Programmer_CLI -c port=SWD -w build/Debug/11111.bin 0x08000000 -rst
```

也可以在 STM32CubeIDE 中打开 `22222.ioc` 对应工程 Build & Flash。编译设置：`-mcpu=cortex-m3`、`--specs=nano.specs`、`-Wl,--gc-sections`、`-T STM32F103XX_FLASH.ld`、宏 `USE_HAL_DRIVER` + `STM32F103xB`；Debug `-O0 -g3`，Release `-Os -g0`。

联调建议：先只上电本节点与网关，在串口/网关日志里确认 `0x702` 心跳被识别，再用 MQTT 发 `gw0805/nodeB/cmd/motor` 负载 `"1,50"`，最后看 `0x102` 的 `pwm` 是否回到 50。

---

## 八、与其他组件的关系

| 对象 | 关系 |
|---|---|
| [`../../README.md`](../../README.md) | 平台总览与构建总说明 |
| [`../gateway/README.md`](../gateway/README.md) | 网关：下发 `0x302`/`0x312`/`0x322`/`0x332`，每秒用 `0x703` 喂本节点的电机保活 |
| [`../collect-node/README.md`](../collect-node/README.md) | 采集节点 A（`NODE_A = 1`），本工程订阅其 `0x101` 与 `0x701` |
| [`../door-node/README.md`](../door-node/README.md) | 门锁节点（ESP-NOW，独立扩展，不接 CAN） |
| [`../../protocol/protocol.h`](../../protocol/protocol.h) | 协议真源；本工程副本在 `Core/Inc/protocol.h`，由 `tools/sync_contract_copies.py` 同步 |
| [`../../protocol/generated/contract.md`](../../protocol/generated/contract.md) | 帧表与命令映射的可读版本 |
| [`../../protocol/README.md`](../../protocol/README.md) | 契约生成与校验流程 |
| [`../../tools/test_contract.py`](../../tools/test_contract.py) | 主机侧协议自测（含 `cid`、急停取值、载荷越界等用例） |
| [`../../host/desktop-console`](../../host/desktop-console) | 下发命令、展示 `nodeB/telemetry` 的上位机 |
| [`../../host/mobile-app`](../../host/mobile-app) | 手机端远程控制入口 |
| [`../../docs/architecture.md`](../../docs/architecture.md) | 分层视图与下行数据流 |
| [`../../docs/troubleshooting/04-CAN故障排查-网络抖动拖垮CAN侧.md`](../../docs/troubleshooting/04-CAN故障排查-网络抖动拖垮CAN侧.md) | 网关侧发布阻塞如何导致本节点误判掉线的复盘 |

改协议时同样只同步副本，不手改 `Core/Inc/protocol.h`：

```bash
python tools/gen_contract.py            # 若改了 contract.json 中的帧/主题
python tools/sync_contract_copies.py    # 把 protocol/protocol.h 同步到本工程
python tools/test_contract.py           # 主机侧自测
```

---

## 九、已知限制与注意事项

以下均为代码中确实存在的状态：

1. **模式切换、目标值、PID 都没有接收端。** 下行控制帧里 `canRxTask` 只处理 `0x302`，契约中发给 B 的 `0x312 CTRL_TARGET_CONTROL`、`0x322 CTRL_PID_CONTROL`、`0x332 CTRL_MODE_CONTROL` 没有对应分支，收到即丢弃；`0x102` 的 `mode` 因此恒为 `MODE_MANUAL`。
2. **状态位只有两位有生产者。** `B_GetFlags()` 只置 `ST_COMM_OK`（3 秒内见过 `0x703`）与 `ST_ESTOP`；`ST_THRESH`、`ST_ALARM` 的代码位置留空，注释写明"还没有生产者……现在硬填就是造假位"。
3. **电机状态无反馈来源。** 没有编码器也没有电流采样，`0x102` 的 `motor` 与 `pwm` 都由 `Motor_GetSpeed()` 的软件记录值推出；`pwm` 不是用 `PWM_GetDuty()` 读 CCR 反算出来的实际值（该函数在本工程中未被调用）。
4. **ADC、串口、按键三条外设初始化后无人使用。** `main.c` 里调用了 `ADC_DMA_Init()`、`Serial_Init()`、`Key_Init()`，但全工程没有任何地方调用 `ADC_DMA_GetValue()`、`Serial_SendData()`、`Serial_SendString()`，也没有读取 `g_sample_enable`；`PWM_GetDuty()` 同样未被调用。
5. **`CMakeLists.txt` 的 `CMAKE_PROJECT_NAME` 是 `11111`。** 与 `22222.ioc`、目录名 `control-node` 都不一致（明显是从采集节点复制过来的），因此产物是 `build/Debug/11111.elf` 而不是 `22222.elf`。
6. **软件 I2C 的延时基数与实际主频不一致。** `iic.c` 定义 `CPU_FREQ_MHZ = 72`，而本工程 `AHB ÷2` 后 HCLK = 36 MHz，DWT `CYCCNT` 按内核时钟计数，`Delay_us()` 实际约为标称值的两倍。
7. **PWM 载频与注释的假设可能不一致。** `pwm.c` 注释按"定时器时钟 72 MHz / (71+1) = 1 MHz，再 /1000 = 1 kHz"推算；本工程 APB1 分频为 1、PCLK1 = 36 MHz，F1 的定时器时钟在 APB 不分频时不额外倍频。寄存器值是确定的（`Prescaler = 71`、`Period = 1000 - 1`），实际载频建议用示波器或 `PWM_GetDuty()` 实测确认，不要直接采信注释里的 1 kHz。
8. **`Error_Handler()` 先关中断再 `HAL_Delay()`。** 函数体为 `__disable_irq()` 后 `while (1) { HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_8); HAL_Delay(200); }`，而 `HAL_Delay` 依赖 SysTick 中断推进 `uwTick`；进入该路径后不能按预期闪灯，实际是停机等待复位。
9. **堆只有 8 KB。** `configTOTAL_HEAP_SIZE = 8192`，5 个任务（栈合计约 4 KB）加 TCB 与 `canRxQueue` 都从这里分配；`MX_FREERTOS_Init()` 里对每个 `osThreadNew` 的返回值都做了 NULL 检查并在失败时进 `Error_Handler()`。
10. **`22222.ioc` 只登记了 SWD 与 LSE 引脚。** 文件内 `Mcu.Pin*` 只包含 `PC14/PC15`（LSE）、`PD0/PD1`（HSE）、`PA13/PA14`（SWD）与两个虚拟引脚；电机方向脚、PWM、CAN、USART2、ADC、软件 I2C、按键、LED 的引脚全部写在 `motor.c`、`pwm.c`、`stm32f1xx_hal_msp.c`、`gpio.c`、`iic.c` 里，用 CubeMX 重新生成代码可能覆盖这些手写配置。
