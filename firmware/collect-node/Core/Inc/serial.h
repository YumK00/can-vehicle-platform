#ifndef __SERIAL_H
#define __SERIAL_H
#include "stm32f1xx_hal.h"

// UART句柄：全局变量，保存USART2配置与运行状态
extern UART_HandleTypeDef huart2;

// 初始化USART2：PA2=TX、PA3=RX，波特率115200，8N1
void Serial_Init(void);
// 阻塞发送一串数据：发完(或超时)才返回
void Serial_SendData(const uint8_t *pTxData, uint16_t Size, uint32_t Timeout);

void Serial_SendString(const char *pTxData,uint32_t Timeout);

// 阻塞接收Size个字节，返回HAL_OK表示收全了，HAL_TIMEOUT表示超时没收满
HAL_StatusTypeDef Serial_Receive(uint8_t *pRxData, uint16_t Size, uint32_t Timeout);

// ==========帧协议定义：用来分辨hex数据和判断数据完整性==========
#define FRAME_HEAD1     0xAA    // 帧头第1字节
#define FRAME_HEAD2     0x55    // 帧头第2字节
#define FRAME_MAX_LEN   64      // 数据区最大字节数(超了判定为错帧)

// 帧格式接收：按 [帧头0xAA 0x55][长度][数据...][校验和] 解析一帧hex数据
// 成功返回1并填好pData/pLen，失败返回0
uint8_t Serial_ReceiveFrame(uint8_t *pData, uint8_t *pLen, uint32_t Timeout);
// 帧格式发送：把pData里的Len字节自动打包成 [0xAA 0x55][长度][数据][校验和] 发出去
void Serial_SendFrame(const uint8_t *pData, uint8_t Len, uint32_t Timeout);
#endif
