#ifndef __ADC_DMA_H
#define __ADC_DMA_H
#include "stm32f1xx_hal.h"

// 通道数：PA0(IN0)、PA1(IN1)共2个
#define ADC_DMA_CH_NUM  2U
 

// DMA循环缓冲区：DMA持续刷新，buf[0]=CH0(PA0/IN0)、buf[1]=CH1(PA1/IN1)，原始值0~4095
// 主循环直接读它即可，不用手动读DR寄存器
extern volatile uint16_t adc_dma_buf[ADC_DMA_CH_NUM];

// 初始化ADC1双通道连续扫描+DMA，并启动转换
void ADC_DMA_Init(void);
// 读某通道ADC原始值(0~4095)：参数0=PA0/IN0，1=PA1/IN1
uint16_t ADC_DMA_GetValue(uint8_t ch);

#endif /* __ADC_DMA_H */
