// #include "timer_extern_interrupt.h"

// // 定时器句柄：全局变量，保存TIM2配置与运行状态，中断回调函数会访问该句柄
// TIM_HandleTypeDef tim2;
// // 溢出中断标志：外部脉冲计数达到ARR值后置1
// // volatile修饰：该变量会在中断回调中修改，禁止编译器对该变量做优化
// volatile uint8_t tim2_etr_overflow_flag = 0;

// /**
//  * @brief  TIM2外部时钟模式2初始化，使用ETR引脚PA0接收外部脉冲作为计数时钟
//  * @note   硬件过程外部脉冲 → PA0 (GPIO 输入) → ETR 滤波 → ETR 预分频 → 边沿极性选择 → 
//              **作为 CNT 计数时钟**CNT ++ → CNT == ARR→ 发中断请求 → NVIC→
//                CPU 进入 TIM2_IRQHandler → HAL 回调 → 设置软件 flag

//  核心寄存器:     PSC预分频器，对定时器内核时钟分频，控制 CNT 自增速度。
//                 ARR自动重载，CNT 计数等于 ARR，触发更新事件，CNT 清零。
//                 CNT计数器，一直在自动累加
//  * @retval None
//              外部时钟模式2原理：ETR引脚上的外部脉冲直接驱动CNT计数器
//  *         每检测到1个有效上升沿，CNT寄存器自动+1；
//  *         不需要EXTI外部中断，不需要定时器CH捕获通道，仅使用ETR专用输入引脚
//  *         当CNT计数值等于ARR自动重载值时，产生更新溢出事件，触发定时器更新中断
//  *         STM32F1：TIM2_ETR复用引脚为PA0
//  */
// void TIM2_ExternClockMode2_Init(void)
// {
//     // 开启GPIOA外设时钟，PA0为TIM2_ETR输入引脚
//     __HAL_RCC_GPIOA_CLK_ENABLE();
//     // 开启TIM2定时器外设时钟
//     __HAL_RCC_TIM2_CLK_ENABLE();

//     GPIO_InitTypeDef GPIO_InitStruct = {0};
//     // 配置PA0为普通输入模式，作为TIM2_ETR专用输入
//     // 注意：不能配置为GPIO_MODE_IT_RISING(EXTI外部中断)，ETR由定时器硬件直接采样
//     GPIO_InitStruct.Pin = GPIO_PIN_0;
//     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
//     GPIO_InitStruct.Pull = GPIO_NOPULL;        // 无上下拉，外部信号自行处理电平
//     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//     HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

//     // TIM2时基单元配置
//     tim2.Instance = TIM2;                      // 指定定时器实例为TIM2
//     tim2.Init.Prescaler = 0;                  // TIM内部PSC预分频器，外部时钟模式2下该分频不起作用
//     tim2.Init.CounterMode = TIM_COUNTERMODE_UP;// 向上计数模式：CNT从0开始递增
//     tim2.Init.Period = 10 - 1;                // ARR自动重载值=9；CNT从0~9，收到10个外部脉冲后溢出中断
//     tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; // 定时器时钟不分频，用于输入采样
//     HAL_TIM_Base_Init(&tim2);                  // 根据句柄初始化TIM2寄存器

//     // ==========核心配置：外部时钟模式2(ETRMODE2)==========
//     TIM_ClockConfigTypeDef sClockSourceConfig = {0};
//     sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_ETRMODE2; // 选择ETR外部时钟模式2
//     sClockSourceConfig.ClockPolarity = TIM_CLOCKPOLARITY_NONINVERTED; // ETR上升沿作为计数有效边沿
//     sClockSourceConfig.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;      // ETR硬件预分频，不对外部脉冲分频
//     sClockSourceConfig.ClockFilter = 0xF;      // ETR输入数字滤波，0xF最大滤波，滤除输入毛刺噪声
//     HAL_TIM_ConfigClockSource(&tim2, &sClockSourceConfig); // 将时钟源配置写入TIM硬件寄存器

//     HAL_TIM_Base_Start_IT(&tim2);             // 启动TIM2计数器，同时使能更新溢出中断UIE

//     // NVIC内核中断配置
//     HAL_NVIC_SetPriority(TIM2_IRQn, 0 ,0);    // 设置TIM2中断抢占优先级0，子优先级0
//     HAL_NVIC_EnableIRQ(TIM2_IRQn);            // 使能TIM2中断通道，允许中断上报Cortex内核
// }

// /**
//  * @brief  TIM2中断服务函数
//  * @note   定时器产生中断后CPU自动跳转到此函数
//  *         调用HAL库统一中断处理函数，完成硬件中断标志清除，调用周期溢出回调
//  * @warning 注意：STM32工程模板stm32f1xx_it.c内部默认存在TIM2_IRQHandler，两处不能同时定义，会编译重复定义报错
//  */
// void TIM2_IRQHandler(void)
// {
//     HAL_TIM_IRQHandler(&tim2);
// }

// /**
//  * @brief  定时器周期溢出回调函数，HAL库内部自动调用
//  * @param  htim：定时器句柄指针
//  * @note   CNT计数到ARR，发生更新溢出事件时触发该回调
//  *         中断内部仅置软件标志位，业务逻辑放到main主循环执行，中断中禁止长耗时操作
//  */
// void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
// {
//     // 判断本次溢出中断来自TIM2
//     if (htim->Instance == TIM2)
//     {
//         tim2_etr_overflow_flag = 1;
//     }
// }
