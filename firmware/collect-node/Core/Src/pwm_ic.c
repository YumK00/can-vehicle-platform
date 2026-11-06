// #include "pwm_ic.h"
// #include "stm32f103xb.h"
// #include "stm32f1xx_hal_gpio.h"
// #include "stm32f1xx_hal_rcc.h"
// #include "stm32f1xx_hal_tim.h"

// // TIM1句柄：PWM输出(PA8)
// TIM_HandleTypeDef tim1;
// // TIM2句柄：输入捕获测频(PA1)
// TIM_HandleTypeDef tim2;

// // TIM2计数频率(Hz)：TIM2时钟72MHz(APB1=36MHz分频2时定时器×2) / 预分频72 = 1MHz，即每计一个数=1us
// #define TIM2_COUNTER_HZ  1000000U

// // 测得的PWM频率(Hz)与周期(us)，输入捕获中断里更新，主循环读去刷新OLED
// volatile uint32_t pwm_freq_hz   = 0;
// volatile uint32_t pwm_period_us = 0;
// // 新数据就绪标志：捕获完成置1，主循环显示后清0
// volatile uint8_t  pwm_capture_ready = 0;
// // 跳过第一次捕获的标志：第一次CCR是"启动到第一个沿"的时间，不是周期
// static uint8_t pwm_first_capture = 1;

// /**
//  * @brief  TIM2_CH2输入捕获初始化，用"从模式复位"测量PWM频率(测周法)
//  * @note   测周法原理：捕获两个相邻上升沿之间的计数个数，就是信号周期，
//  *         f = 计数频率 / 周期。
//  *         这里用从模式复位(Slave Reset)：每个上升沿先把CNT存进CCR2再清零CNT，
//  *         所以CCR2直接等于周期，软件不用做两次捕获相减。
//  *         触发源是TI2FP2(PA1滤波后信号)，属于外部信号测频，只需要配Slave，
//  *         不需要配Master。
//  * @retval None
//  */
// void PWM_IC_Init(void)
// {
//     // 开启GPIOA时钟，PA1为TIM2_CH2输入捕获引脚
//     __HAL_RCC_GPIOA_CLK_ENABLE();
//     // 开启TIM2定时器时钟
//     __HAL_RCC_TIM2_CLK_ENABLE();

//     // PA1配置为普通输入，作为TIM2_CH2输入捕获
//     GPIO_InitTypeDef gpio = {0};
//     gpio.Mode = GPIO_MODE_INPUT;
//     gpio.Pin = GPIO_PIN_1;              // PA1 = TIM2_CH2
//     gpio.Pull = GPIO_NOPULL;
//     gpio.Speed = GPIO_SPEED_FREQ_HIGH;
//     HAL_GPIO_Init(GPIOA, &gpio);

//     // ==========TIM2时基配置==========
//     tim2.Instance = TIM2;
//     tim2.Init.CounterMode = TIM_COUNTERMODE_UP;    // 向上计数
//     tim2.Init.Prescaler = 72 - 1;                  // 计数频率=72MHz/72=1MHz，分辨率1us
//     tim2.Init.Period = 65535;                      // ARR最大，65.5ms才溢出，足够测低频
//     tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
//     HAL_TIM_IC_Init(&tim2);

//     // ==========输入捕获通道配置：CH2直接输入TI2，上升沿捕获==========
//     TIM_IC_InitTypeDef ic = {0};
//     ic.ICFilter = 0xF;                     // 数字滤波，滤除输入毛刺
//     ic.ICPolarity = TIM_ICPOLARITY_RISING; // 上升沿触发捕获
//     ic.ICPrescaler = TIM_ICPSC_DIV1;       // 每个沿都捕获，不跳沿
//     ic.ICSelection = TIM_ICSELECTION_DIRECTTI; // 直接输入TI2
//     HAL_TIM_IC_ConfigChannel(&tim2, &ic, TIM_CHANNEL_2);

//     // ==========从模式：复位模式(测周法核心)==========
//     // 用TI2FP2作触发源，复位模式：每个上升沿先捕获CNT到CCR2再清零CNT
//     TIM_SlaveConfigTypeDef slave = {0};
//     slave.SlaveMode = TIM_SLAVEMODE_RESET;             // 复位模式
//     slave.InputTrigger = TIM_TS_TI2FP2;                // 触发源：TI2滤波后信号
//     slave.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;// 上升沿触发
//     slave.TriggerFilter = 0x0F;                        // 触发输入滤波
//     HAL_TIM_SlaveConfigSynchro(&tim2, &slave);

//     // 启动CH2输入捕获，并使能捕获中断
//     HAL_TIM_IC_Start_IT(&tim2, TIM_CHANNEL_2);

//     // NVIC配置TIM2中断
//     HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
//     HAL_NVIC_EnableIRQ(TIM2_IRQn);
// }

// /**
//  * @brief  TIM1_CH1 PWM输出(PA8)，作为被测信号源
//  * @note   本工程TIM1挂APB2=72MHz：计数频率=72MHz/72=1MHz，PWM=1MHz/1000=1kHz
//  * @retval None
//  */
// void product_pwm_boxing(void)
// {
//     // 开启GPIOA外设时钟，PA8为TIM1_CH1复用引脚
//     __HAL_RCC_GPIOA_CLK_ENABLE();
//     // 开启TIM1高级定时器外设时钟
//     __HAL_RCC_TIM1_CLK_ENABLE();

//     GPIO_InitTypeDef GPIO_InitStruct = {0};
//     GPIO_InitStruct.Pin = GPIO_PIN_8;
//     GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;        // 复用推挽输出
//     GPIO_InitStruct.Pull = GPIO_NOPULL;
//     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//     HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

//     tim1.Instance = TIM1;
//     tim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
//     tim1.Init.CounterMode = TIM_COUNTERMODE_UP;
//     tim1.Init.Prescaler = 72 - 1;                  // 计数频率=72MHz/72=1MHz
//     tim1.Init.Period = 1000 - 1;                   // PWM频率=1MHz/1000=1kHz
//     tim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
//     tim1.Init.RepetitionCounter = 0;
//     HAL_TIM_PWM_Init(&tim1);

//     TIM_OC_InitTypeDef oc = {0};
//     oc.OCMode = TIM_OCMODE_PWM1;
//     oc.Pulse = 500;                                // 占空比50%
//     oc.OCPolarity = TIM_OCPOLARITY_HIGH;
//     oc.OCFastMode = TIM_OCFAST_DISABLE;
//     oc.OCIdleState = TIM_OCIDLESTATE_RESET;
//     HAL_TIM_PWM_ConfigChannel(&tim1, &oc, TIM_CHANNEL_1);
//     __HAL_TIM_MOE_ENABLE(&tim1);                   // 高级定时器主输出使能
//     HAL_TIM_PWM_Start(&tim1, TIM_CHANNEL_1);
// }

// /**
//  * @brief  TIM2中断服务函数
//  * @note   捕获中断产生时CPU自动跳到这里，调HAL统一处理(清标志、调回调)
//  */
// void TIM2_IRQHandler(void)
// {
//     HAL_TIM_IRQHandler(&tim2);
// }

// /**
//  * @brief  输入捕获回调，HAL_TIM_IRQHandler内部自动调用
//  * @param  htim：定时器句柄
//  * @note   复位模式下CCR2=周期计数个数，f=计数频率/周期。
//  *         中断里只做简单计算+置标志，显示放到主循环(OLED的I2C慢，别在中断里刷屏)
//  */
// void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
// {
//     // 判断是TIM2的CH2产生的捕获中断
//     if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
//     {
//         // 复位模式下CCR2直接等于一个周期的计数个数(=周期us，因为1MHz计数)
//         uint32_t period = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);

//         // 第一次捕获的CCR是"启动到第一个上升沿"的时间，不是周期，跳过
//         if (pwm_first_capture)
//         {
//             pwm_first_capture = 0;
//             return;
//         }

//         // 周期为0异常，跳过防止除零
//         if (period == 0)
//         {
//             return;
//         }

//         pwm_period_us = period;
//         pwm_freq_hz   = TIM2_COUNTER_HZ / period;  // f = 计数频率 / 周期
//         pwm_capture_ready = 1;                     // 置就绪标志，主循环去刷新OLED
//     }
// }
