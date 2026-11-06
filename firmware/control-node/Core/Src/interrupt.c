#include "interrupt.h"
#include "FreeRTOS.h"   // pdMS_TO_TICKS
#include "task.h"       // xTaskGetTickCountFromISR

/* ============================================================
 * 【按键开关】—— 中断里翻转一个开关变量
 *
 * 数据流：
 *   按下按键(PB12) → 下降沿 → EXTI15_10_IRQHandler
 *      → HAL_GPIO_EXTI_IRQHandler → HAL_GPIO_EXTI_Callback
 *      → g_sample_enable 翻转一次(0↔1)
 *      → photoAdcTask 周期轮询它：开着就采样，关着就跳过
 *
 * 为什么用中断：按键不按的时候 CPU 完全不用管，按下去才进一次中断。
 * ============================================================ */

/* 采样开关：0=关，1=开。
 * 定义在 interrupt.c、声明在 interrupt.h，别的文件 extern 引用。
 * 中断里写、任务里读 → 必须 volatile，让编译器每次都读真实内存值，
 * 而不是把上次读到的值缓存进寄存器。uint8_t 在 M3 上读写是原子的，
 * 所以不用再加临界区。 */
volatile uint8_t g_sample_enable = 0;

/**
 * @brief  初始化 PB12 按键，配成下降沿触发的 EXTI 中断
 * @note   PB12 接按键、另一端接 GND：松开被上拉电阻拉高，按下变低电平，
 *         "高→低"就是下降沿。
 */
void Key_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_12;
    gpio.Mode  = GPIO_MODE_IT_FALLING;   // IT = InTerrupt，下降沿触发
    gpio.Pull  = GPIO_PULLUP;            // 内部上拉，没按键保持高电平
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* NVIC：让 CPU 收到这个中断。
     * 优先级 5：中断里用了 xTaskGetTickCountFromISR()(FromISR 类 API)，
     * 按 FreeRTOS 规矩这类中断优先级必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(=5)，
     * 设成 0 会破坏内核屏蔽机制，启动时 configASSERT 卡死。 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/**
 * @brief  EXTI 线 10~15 共用的中断服务函数(名字必须和向量表一致)
 */
void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
}

/**
 * @brief  GPIO 外部中断回调(HAL 弱函数，重写它写自己的逻辑)
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_12)
    {
        /* ---- 软件消抖 ----
         * 机械按键按下瞬间触点会"弹跳"，电平抖出一串快速跳变，
         * 每次都触发一次中断。不消抖的话按一下开关会来回翻好几次。
         * 做法：两次有效按键至少间隔 20ms，更近的当成抖动丢掉。
         * xTaskGetTickCountFromISR() 取当前 tick，中断里用是安全的。 */
        static TickType_t last_key_tick = 0;
        TickType_t now = xTaskGetTickCountFromISR();
        if ((now - last_key_tick) >= pdMS_TO_TICKS(20))
        {
            last_key_tick = now;
            g_sample_enable = !g_sample_enable;   // 有效按一次，开关翻转一次
        }
    }
}
