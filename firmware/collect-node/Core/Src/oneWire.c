#include "oneWire.h"

#define WirePort  GPIOC
#define WirePin   GPIO_PIN_15
#define Wire_W(x) HAL_GPIO_WritePin(WirePort,WirePin,x?GPIO_PIN_SET:GPIO_PIN_RESET)
#define Wire_R()  HAL_GPIO_ReadPin(WirePort,WirePin)

#define Wire_OutPut() do{GPIO_InitTypeDef GPIO_InitStruct = { 0 };\
                        GPIO_InitStruct.Pin = WirePin;\
                        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;\
                        GPIO_InitStruct.Pull = GPIO_NOPULL;\
                        GPIO_InitStruct.Speed=GPIO_SPEED_LOW;\
                        HAL_GPIO_Init (WirePort, &GPIO_InitStruct);\
                        }while (0)

#define Wire_Input()  do{GPIO_InitTypeDef GPIO_InitStruct = { 0 };\
                        GPIO_InitStruct.Pin = WirePin;\
                        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;\
                        GPIO_InitStruct.Pull = GPIO_NOPULL;\
                        HAL_GPIO_Init (WirePort, &GPIO_InitStruct);}while (0)

/* 假设 SYSCLK = 72MHz，每次循环约 8~10 个周期 */
static void Delay_us (uint32_t us) {
    while (us--) {
        for (volatile uint32_t i = 0; i < 8; i++);
    }
}

void OneWire_Init (void) {
    Wire_OutPut ();
    Wire_W (1);
}

// 通讯开始时主机(MCU)拉低总线500us后释放总线，延时20-40us后主机开始检测从机（DHT22）的响应信号。
void OneWire_Start (void) {
    Wire_W (0);
    Delay_us (1000); //480us~960us
    Wire_W (1);
    Delay_us (30); //15us~60us
    Wire_Input ();
}

uint8_t OneWire_Ack (void) {
    uint8_t timeout = 0;
    //等待响应信号 拉低
    while (Wire_R () != GPIO_PIN_RESET && timeout < 100) {
        timeout++;
        Delay_us (1);
    }
    if (timeout >= 100) return 1;
    // 拉高
    else timeout = 0;
    while (Wire_R () != GPIO_PIN_SET && timeout < 100) {
        timeout++;
        Delay_us (1);
    }
    if (timeout >= 100) return 1;

    else return 0;
}

uint8_t OneWire_R_Bit (void) {
    uint8_t timeout = 0;
    while (Wire_R () && timeout < 100) {
        timeout++;
        Delay_us (1);
    }
    if (timeout >= 100) return 0;
    timeout = 0;
    while (!Wire_R () && timeout < 100) {
        timeout++;
        Delay_us (1);
    }
    if (timeout >= 100)return 0;
    Delay_us (40);
    // 此时若仍为高，说明是"1"（高电平持续约 70us）
    return (Wire_R () == GPIO_PIN_SET) ? 1 : 0;
}

void OneWire_R_Byte (uint8_t * byte) {
    //读取一个字节
    for (uint8_t temp = 0;temp < 8;temp++) {
        *byte = (*byte << 1) | OneWire_R_Bit ();  // 先左移，再填入新位
    }
}


