#ifndef __ONEWIRE_H
#define __ONEWIRE_H

#include "stm32f1xx_hal.h"

void OneWire_Init (void);

void OneWire_Start (void);
uint8_t OneWire_Ack (void);

uint8_t OneWire_R_Bit (void);
void OneWire_R_Byte (uint8_t * byte);

#endif