#include "pwm.h"
#include "main.h"      /* Error_Handler() */

/* ============================================================
 * 【引脚/定时器配置区 —— 换硬件只改这一段】
 *
 * 为什么是 TIM3 + PA6：
 *   - TIM4 不能选：HAL 的时基就靠它(stm32f1xx_hal_timebase_tim.c 里用 TIM4
 *     产生中断来调 HAL_IncTick)，再拿去输出 PWM 会打架。
 *   - TIM2 的 CH1~CH4 默认在 PA0~PA3：PA0/PA1 给了 ADC(红外+光敏)、
 *     PA2/PA3 给了 USART2，四个通道全满，想用只能开重映射，没必要。
 *   - TIM1 是高级定时器，拿来做单路普通 PWM 属于杀鸡用牛刀。
 *   - PA6 = TIM3_CH1 是默认引脚，不需要碰 AFIO 重映射；F1 的 GPIO 也没有
 *     "AF 编号"要填，Mode 配成复用推挽就已经切到定时器上了。
 *
 * 其它可选(TIM3 的默认引脚)：
 *   PA6=CH1   PA7=CH2   PB0=CH3   PB1=CH4
 * 换引脚只改下面这几个宏，下面的代码一个字都不用动。
 * ============================================================ */
#define PWM_TIM              TIM3
#define PWM_TIM_CLK_ENABLE   __HAL_RCC_TIM3_CLK_ENABLE
#define PWM_GPIO_PORT        GPIOA
#define PWM_GPIO_CLK_ENABLE  __HAL_RCC_GPIOA_CLK_ENABLE
#define PWM_GPIO_PIN         GPIO_PIN_6            /* PA6 = TIM3_CH1 */
#define PWM_CHANNEL          TIM_CHANNEL_1

/* PWM 频率 = 定时器时钟 / PWM_PERIOD
 *   定时器时钟 = 72MHz / (PWM_PRESCALER + 1) = 72MHz / 72 = 1MHz
 *   PWM 频率   = 1MHz / 1000 = 1kHz
 * 1kHz 是电机驱动(L298N/TB6612 之类)常见的 PWM 输入范围。 */
#define PWM_PRESCALER        71u
#define PWM_PERIOD           1000u

/* 句柄只在本文件用，加 static 免得和别的文件里的同名句柄撞车 */
static TIM_HandleTypeDef htim_pwm;

/**
 * @brief  初始化 PWM 输出
 * @note   上电占空比固定为 0(电机不转)，要动就调 SetPWMDuty()。
 *         这一步只配置硬件，不涉及任何业务逻辑。
 * @retval None
 */
void PWM_Init(void)
{
    /* ---- 1. 时钟：GPIOA + TIM3。APB1 上的外设各开各的钟，开错一个都不出波形 ---- */
    PWM_GPIO_CLK_ENABLE();
    PWM_TIM_CLK_ENABLE();

    /* ---- 2. 引脚：复用推挽。复用(AF)的意思是这个引脚不再听 ODR，改由定时器驱动 ---- */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = PWM_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;   /* 方波要求边沿陡，用 HIGH；LOW 会让边沿变缓 */
    gpio.Pull  = GPIO_NOPULL;            /* 推挽输出本身有上下两个管子，不需要上下拉 */
    HAL_GPIO_Init(PWM_GPIO_PORT, &gpio);

    /* ---- 3. 时基：分频出 1MHz 计数时钟，再数 1000 个变一个周期 ---- */
    htim_pwm.Instance           = PWM_TIM;
    htim_pwm.Init.Prescaler     = PWM_PRESCALER;
    htim_pwm.Init.CounterMode   = TIM_COUNTERMODE_UP;   /* 向上计数：0 → ARR → 0 */
    htim_pwm.Init.Period        = PWM_PERIOD - 1u;      /* 写进 ARR。CNT 数 0~999 共 1000 个点 */
    htim_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; /* ARR 固定不变，无需缓冲 */
    if (HAL_TIM_PWM_Init(&htim_pwm) != HAL_OK)
    {
        Error_Handler();
    }

    /* ---- 4. 通道：PWM 模式 1。CNT < CCR 输出有效电平(高)，反之输出低 ---- */
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;                    /* 初始 CCR=0 → 占空比 0% */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;  /* 有效电平为高，别改成 LOW，否则占空比含义会反过来 */
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim_pwm, &oc, PWM_CHANNEL) != HAL_OK)
    {
        Error_Handler();
    }

    /* 打开 CCR 的预装载：新的占空比会在下一个完整周期开始时才生效，
     * 避免在周期中间改 CCR 产生一个宽窄不对的畸形脉冲。 */
    __HAL_TIM_ENABLE_OCxPRELOAD(&htim_pwm, PWM_CHANNEL);

    /* ---- 5. 启动输出：CCER 的 CCxE 置位，引脚开始出波形 ---- */
    HAL_TIM_PWM_Start(&htim_pwm, PWM_CHANNEL);
}

/**
 * @brief  设置 PWM 占空比
 * @param  percent：占空比百分比 0~100，大于 100 会被限幅到 100
 * @note   收百分比而不是 CCR 计数值，是为了和协议里 B_STATUS 的 B1 字段
 *         (0~100 的百分比，见 protocol.h)直接对齐，调用方不用自己换算。
 * @retval None
 */
void SetPWMDuty(uint8_t percent)
{
    if (percent > 100u)
    {
        percent = 100u;
    }

    /* 百分比 → CCR。
     * 一个周期里 CNT 会经历 0 ~ PWM_PERIOD-1 共 PWM_PERIOD 个点，
     * 所以满占空比要写 PWM_PERIOD，而不是 ARR(PWM_PERIOD-1)：
     * 写 ARR 的话 100% 永远差一格，最大只能到 99.9%。 */
    __HAL_TIM_SET_COMPARE(&htim_pwm, PWM_CHANNEL,
                          (uint32_t)percent * PWM_PERIOD / 100u);
}

/**
 * @brief  读回当前占空比
 * @note   直接拿 CCR 反算，不额外存一份状态，永远和硬件真实值一致。
 * @retval uint8_t：占空比百分比 0~100
 */
uint8_t PWM_GetDuty(void)
{
    return (uint8_t)(__HAL_TIM_GET_COMPARE(&htim_pwm, PWM_CHANNEL) * 100u / PWM_PERIOD);
}

/**
 * @brief  等到当前 PWM 周期结束
 * @note   上面开了 CCR 预装载，所以 SetPWMDuty() 写进去的值要等下一个
 *         更新事件(计数到 ARR 归零)才真正变成引脚上的电平。这个函数就是
 *         等那一刻 —— 换向时必须先停稳再翻方向脚(见 motor.c)。
 *
 *         这里读的 SR 寄存器在头文件里是 __IO(volatile)，所以编译器
 *         不会像之前那个空的 Delay_us 循环一样把它优化掉。
 * @retval None
 */
void PWM_WaitNextPeriod(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim_pwm, TIM_FLAG_UPDATE);
    while (__HAL_TIM_GET_FLAG(&htim_pwm, TIM_FLAG_UPDATE) == RESET)
    {
        /* 空转等硬件置标志位 */
    }
}
