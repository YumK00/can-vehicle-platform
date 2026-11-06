#ifndef MOTOR_H_
#define MOTOR_H_

#include "pwm.h"

/* 初始化电机：PWM 速度通道(PA6/TIM3_CH1) + 两个方向脚(PA4/PA5)，
 * 上电状态是"停"，两个方向脚都是低电平。 */
void Motor_Init(void);

/* 设置速度，带符号：-100(全速反转) ~ 0(停) ~ +100(全速正转)。
 * 符号是方向，绝对值是占空比百分比。负号只是给人的接口方便，
 * PWM 那边拿到的永远是 0~100 的正占空比。 */
void Motor_SetSpeed(int16_t speed);

/* 读回上一次下发的速度，-100~+100。*/
int16_t Motor_GetSpeed(void);

#endif
