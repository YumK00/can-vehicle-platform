#ifndef __INTERRUPT_H
#define __INTERRUPT_H
#include "stm32f1xx_hal.h"

/* 采样开关：0=关，1=开。中断里翻转，任务里读。 */
extern volatile uint8_t g_sample_enable;

/* 按键(PB12)外部中断初始化 */
void Key_Init(void);

#endif /* __INTERRUPT_H */
