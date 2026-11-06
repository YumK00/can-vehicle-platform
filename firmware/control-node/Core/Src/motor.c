#include "motor.h"
#include "pwm.h"

/* ============================================================
 * 【方向脚配置区 —— 换硬件只改这一段】
 *
 * 反转不是靠"负占空比"实现的(PWM 只有 0~100%，物理上不可能为负)，
 * 而是靠 H 桥这两个 GPIO 的组合：
 *
 *      IN1  IN2
 *       0    0   → 停(滑行，电机两端悬空)
 *       1    0   → 正转
 *       0    1   → 反转
 *       1    1   → 刹车(绕组短路，这里不用)
 *
 * 方向由这两个脚决定，转多快由 PA6 的 PWM 占空比决定，两件事分开管。
 * ============================================================ */
#define MOTOR_PORT   GPIOA
#define MOTOR_PIN1   GPIO_PIN_4   /* 接驱动板的 IN1 / AIN1 */
#define MOTOR_PIN2   GPIO_PIN_5   /* 接驱动板的 IN2 / AIN2 */

/* 上一次下发的速度。符号=方向，绝对值=占空比。
 * 为什么要存：① 判断"这次是不是换向"，只有换向才需要停顿；
 *             ② GetSpeed() 给别的任务读一个完整的快照。 */
static volatile int16_t s_speed = 0;

/**
 * @brief  初始化电机
 * @note   速度通道交给 pwm.c（它管定时器），这里只管两个方向脚。
 * @retval None
 */
void Motor_Init(void)
{
    PWM_Init();                          /* PA6 = TIM3_CH1，1kHz，占空比 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE(); 
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = MOTOR_PIN1 | MOTOR_PIN2;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;    /* 方向脚是普通推挽输出，不经过定时器 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MOTOR_PORT, &gpio);

    HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN1 | MOTOR_PIN2, GPIO_PIN_RESET);
    s_speed = 0;
}

/**
 * @brief  设置电机速度
 * @param  speed：-100 ~ +100。负=反转，0=停，正=正转；绝对值是占空比百分比
 * @note   同方向调速只改占空比；只有换向(含从静止起步)才停下来等一个周期，
 *         所以正常调速不会有额外的 1ms 停顿。
 * @retval None
 */
void Motor_SetSpeed(int16_t speed)
{
    int8_t  dir;
    int8_t  prev_dir;
    uint8_t duty;

    /* ---- 1. 限幅：对外收 -100~+100，超了夹住 ---- */
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;

    /* ---- 2. 停：占空比归零 + 两个方向脚拉低(滑行) ---- */
    if (speed == 0)
    {
        SetPWMDuty(0);
        HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN1 | MOTOR_PIN2, GPIO_PIN_RESET);
        s_speed = 0;
        return;
    }

    dir  = (speed > 0) ? 1 : -1;
    duty = (uint8_t)((speed > 0) ? speed : -speed);   /* 占空比永远取绝对值 */

    prev_dir = (s_speed > 0) ? 1 : ((s_speed < 0) ? -1 : 0);

    /* ---- 3. 只有真的换向才需要"先停一下" ----
     *
     * 【为什么要停】H 桥在大占空比下直接翻转 IN1/IN2，等于让电机两端瞬间
     * 反向，反电动势会叠加在电源上，灌出一个很大的电流尖峰 ——
     * 轻则驱动板过流保护跳闸，重则烧功率管。
     *
     * 【为什么不能"写完 0 就立刻翻"】pwm.c 里开了 CCR 预装载，
     * SetPWMDuty(0) 只是写进预装载寄存器，要等下一个更新事件才真正反映到
     * 引脚上(最多一个 PWM 周期 = 1ms)。这期间引脚还停在旧占空比，
     * 这时候翻方向脚等于白停。所以必须等一个周期，让 0% 真的落到引脚上。
     *
     * 【为什么同方向不用等】方向没变，H 桥的输出极性就没变，
     * 中途改的只是电流大小，不存在反向电动势叠加的问题。 */
    if (dir != prev_dir)
    {
        SetPWMDuty(0);
        PWM_WaitNextPeriod();            /* 等到 0% 真的输出到引脚上 */

        if (dir > 0)
        {
            HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN1, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN2, GPIO_PIN_RESET);
        }
        else
        {
            HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN1, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN2, GPIO_PIN_SET);
        }
    }

    /* ---- 4. 给速度。方向脚已经摆好，占空比跟着上去 ---- */
    SetPWMDuty(duty);
    s_speed = speed;
}

/**
 * @brief  读回当前速度
 * @retval int16_t：-100 ~ +100
 */
int16_t Motor_GetSpeed(void)
{
    return s_speed;
}
