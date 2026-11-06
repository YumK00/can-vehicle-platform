#ifndef __PWM_H
#define __PWM_H

#include "stm32f1xx_hal.h"

/* 初始化 PWM 输出（引脚/定时器见 pwm.c 顶部的配置区），上电占空比 0 */
void PWM_Init(void);

/* 设置占空比，单位是百分比 0~100（>100 会被限幅到 100） */
void SetPWMDuty(uint8_t percent);

/* 读回当前占空比百分比 0~100，直接从 CCR 寄存器算出来 */
uint8_t PWM_GetDuty(void);

/* 阻塞等到当前 PWM 周期结束（新写入的占空比真正输出到引脚上为止）。
 * 换向时用：SetPWMDuty(0) 之后要等它生效，才能安全地翻方向脚。
 * 最长等一个 PWM 周期(现在 1kHz → 最多 1ms)。 */
void PWM_WaitNextPeriod(void);

#endif
