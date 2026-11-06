#ifndef __CAN_H
#define __CAN_H
#include "stm32f1xx_hal.h"
#include "cmsis_os2.h"

extern CAN_HandleTypeDef hcan1;

/* ---- 一帧 CAN 报文(标准帧) ----
 * 把整帧打包成一个结构体，才能一次投进队列。
 */
typedef struct {
  uint32_t id;        // 11位标准ID(0~0x7FF)
  uint8_t  len;       // 数据长度 DLC(0~8)
  uint8_t  data[8];   // 数据
} CanFrame;

/* 接收队列：中断回调里 Put、canRxTask 里 Get。
 * 定义在 can.c，在 MX_FREERTOS_Init() 里创建。 */
extern osMessageQueueId_t canRxQueue;

void CAN_Init(void);
uint8_t CAN_Send(uint32_t id, uint8_t *data, uint8_t length);

#endif /* __CAN_H */
