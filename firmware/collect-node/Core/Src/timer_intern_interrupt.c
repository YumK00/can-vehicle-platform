// #include "timer_intern_interrupt.h"

// // TIM2句柄，保存定时器配置、状态等信息
// TIM_HandleTypeDef tim2;  

// // 定时器2中断标志，volatile：告诉编译器变量会在中断里修改，禁止优化
// volatile uint8_t tim2_interrupt_flag = 0;

// /**
//  * @brief  TIM2定时器初始化，配置定时溢出中断
//  * @note   硬件流程：
// *        1. RCC开启TIM2外设时钟；
// *        2. 配置时基单元：预分频PSC、计数模式、自动重载ARR；
// *           本工程为**内部时钟源**，不需要配置定时器CH捕获比较通道，不需要EXTI外部中断；
// *        3. 开启定时器内部更新中断（UIE），计数器溢出产生更新事件；
// *        4. NVIC配置TIM2中断优先级并使能，让中断信号送达内核；
// *        5. 中断触发后进入TIM2_IRQHandler，HAL库清除硬件中断标志并调用溢出回调。

 
 
//  STM32F1：系统SYSCLK=72MHz，APB1预分频=2，PCLK1=36MHz
//  *         硬件倍频：APB1分频>1时，TIM2时钟 = PCLK1*2 = 72MHz
//  *         预分频Prescaler = 7200 - 1，定时器计数频率 = 72M / 7200 = 10000Hz（每计数0.1ms）
//  *         自动重载Period = 10000 - 1，共10000次计数
//  *         溢出时间 = 1000 / 10000Hz = 1s，每1秒触发一次更新中断
//  */
// void TIM2_Init(void){
//     // 开启TIM2外设时钟
//     __HAL_RCC_TIM2_CLK_ENABLE();

//     // 指定定时器实例为TIM2
//     tim2.Instance = TIM2;
//     // 预分频器，分频系数7200
//     tim2.Init.Prescaler = 7200 - 1;
//     // 计数器模式：向上计数（从0增加到自动重载值，溢出后归零）
//     tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
//     // 自动重载值，计数到该值产生更新事件
//     tim2.Init.Period = 1000 - 1;
//     // 时钟分频，不分频，定时器时钟直接作为采样时钟
//     tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
//     // 关闭自动重载预装载，ARR寄存器写入后立即生效，不需要等更新事件
//      这里有一个概念 影子寄存器机制
//     tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

//     // 初始化定时器基础配置
//     HAL_TIM_Base_Init(&tim2);
//     // 启动定时器，并开启更新溢出中断
//     HAL_TIM_Base_Start_IT(&tim2);

//     // 设置TIM2中断优先级：抢占优先级0，子优先级0
//     // 优先级分组在main.c的HAL_Init()中已经完成配置
//     HAL_NVIC_SetPriority(TIM2_IRQn, 0 ,0);
//     // 使能TIM2的NVIC中断通道
//     HAL_NVIC_EnableIRQ(TIM2_IRQn);
// }

// /**
//  * @brief TIM2中断服务函数
//  * @note 发生TIM2中断时，CPU自动进入该函数
//  *        调用HAL库通用中断处理函数，完成中断标志判断、清除，并调用回调函数
//  */
// void TIM2_IRQHandler(void)
// {
//     HAL_TIM_IRQHandler(&tim2);   // HAL 库统一处理：清标志、判断中断源、调回调
// }

// /**
//  * @brief 定时器周期溢出回调函数，HAL_TIM_IRQHandler内部自动调用
//  * @param  htim：定时器句柄指针
//  * @retval 无
//  * @note   定时器计数溢出（到达ARR值）触发此回调
//  */
// void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
// {
//     // 判断是TIM2产生的溢出中断
//     if (htim->Instance == TIM2)
//     {
//         // 置位中断标志，主循环中检测该标志，在主函数执行任务（避免在中断里写长耗时代码）
//         tim2_interrupt_flag = 1;
//     }
// }
