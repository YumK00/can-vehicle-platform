// #include "pwm_oc.h"

// // TIM1定时器句柄：全局变量，保存TIM1配置与运行状态
// TIM_HandleTypeDef tim1;

// // 呼吸灯占空比步进值：每次改变CCR的大小，步进越小呼吸越平滑
// #define BREATH_STEP      10
// // 呼吸灯每步延时（ms）：控制呼吸快慢，延时越大呼吸越慢
// #define BREATH_DELAY_MS  5

// /**
//  * @brief  TIM1_CH1 PWM初始化，用于PA8呼吸灯
// 注：这里输出到gpio是有芯片数据手册中规定的固定引脚TIMx_CHy,所以配置对应引脚和通道即可。
//  * @note   硬件流程：PWM波形 → PA8 (TIM1_CH1 复用推挽输出) → 驱动LED
//  *         PWM波形产生过程就是PWM的核心就是CNT和CCR捕获比较寄存器做硬件比较,
//             通过改变CCR大小可以改变占空比，从而改变引脚的输出值，
//             这个硬件模块就叫OC(Output Compare，输出比较单元)

//     核心寄存器:  PSC预分频器，对定时器内核时钟分频，控制 CNT 自增速度。
//                 ARR自动重载，CNT 计数等于 ARR，触发更新事件，CNT 清零。
//                 CNT计数器，一直在自动累加
//                 CCR捕获比较寄存器，CNT和CCR实时比较这就是OC输出比较核心。
//  *
//  * @retval None
//  */
// void PWM_OC_Init(void)
// {
//     // 开启GPIOA外设时钟，PA8为TIM1_CH1复用引脚
//     __HAL_RCC_GPIOA_CLK_ENABLE();
//     // 开启TIM1高级定时器外设时钟
//     __HAL_RCC_TIM1_CLK_ENABLE();

//     GPIO_InitTypeDef GPIO_InitStruct = {0};
//     GPIO_InitStruct.Pin = GPIO_PIN_8;              // PA8 = TIM1_CH1 复用引脚
//     GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;        // 复用推挽输出：由定时器输出PWM波形
//     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;  // 高速输出，保证PWM边沿陡峭
//     GPIO_InitStruct.Pull = GPIO_NOPULL;            // 无上下拉，推挽输出无需上下拉
//     HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

//     // ==========TIM1时基单元配置==========
//     tim1.Instance = TIM1;                          // 指定定时器实例为TIM1
//     tim1.Init.Prescaler = 72 - 1;                  // 预分频系数
//     tim1.Init.CounterMode = TIM_COUNTERMODE_UP;    // 向上计数模式：CNT从0递增到ARR后归零
//     tim1.Init.Period = 1000 - 1;                   // ARR=999，PWM频率 = 1MHz / 1000 = 1kHz
//     tim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; // 定时器时钟不分频，用于输入采样
//     tim1.Init.RepetitionCounter = 0;               // 高级定时器重复计数器，PWM输出设为0即可
//     tim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; // 关闭影子寄存器，ARR写入立即生效
//     HAL_TIM_OC_Init(&tim1);                       // 根据句柄初始化TIM1寄存器

//     // ==========核心配置：PWM输出通道==========其实就是配置OC比较器(output compare输出比较器)
//     TIM_OC_InitTypeDef sConfigOC = {0};
//     sConfigOC.OCMode = TIM_OCMODE_PWM1;            // OC比较器的比较模式 PWM模式1：CNT < CCR时输出有效电平，CNT >= CCR时输出无效电平
//     sConfigOC.Pulse = 0;                           // 初始比较值CCR=0，占空比0%，LED初始熄灭
//     sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;    // 输出极性：高电平有效
//     sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;     // 关闭快速模式，普通PWM无需快速比较
//     HAL_TIM_OC_ConfigChannel(&tim1, &sConfigOC, TIM_CHANNEL_1); // 将CH1通道PWM配置写入TIM硬件寄存器

//     // 启动TIM1的CH1通道PWM输出
//     HAL_TIM_OC_Start(&tim1, TIM_CHANNEL_1);


//     /*
//     这里pwm模式是oc模式的一种，hal库把pwm的api分离出来了
//     其实也可以用
//     HAL_TIM_PWM_Init;
//     HAL_TIM_PWM_ConfigChannel;
//     HAL_TIM_PWM_Start;
//     */
// }

// /**
//  * @brief  PA8呼吸灯主逻辑：占空比渐亮→渐暗循环
//  * @note   呼吸过程：比较值CCR从0递增到999，占空比0%→100%，LED渐亮；
//  *         再从999递减到0，占空比100%→0%，LED渐暗。
//  *         每次改变CCR后延时BREATH_DELAY_MS，控制呼吸速度。
//  *         本函数包含死循环延时，需在main主循环中调用，不要放进中断。
//  * @retval None
//  */
// void Breath_LED(void)
// {
//     int32_t i;

//     // 呼吸渐亮阶段：占空比从0%逐步增加到100%
//     for (i = 0; i <= 1000; i += BREATH_STEP)
//     {
//         // 直接修改CH1比较寄存器CCR，占空比随之改变，LED亮度变化
//         __HAL_TIM_SET_COMPARE(&tim1, TIM_CHANNEL_1, i);
//         // 每步延时，让亮度变化肉眼可见，形成平滑呼吸效果
//         HAL_Delay(BREATH_DELAY_MS);
//     }

//     // 呼吸渐暗阶段：占空比从100%逐步减小到0%，有符号类型能正常减到0并退出
//     for (i = 1000; i >= 0; i -= BREATH_STEP)
//     {
//         __HAL_TIM_SET_COMPARE(&tim1, TIM_CHANNEL_1, i);
//         HAL_Delay(BREATH_DELAY_MS);
//     }
// }
