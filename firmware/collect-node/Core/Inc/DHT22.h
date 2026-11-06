#ifndef __DHT22_H
#define __DHT22_H

#include "stm32f1xx_hal.h"
#include "oneWire.h"
#include <stdbool.h>

bool DHT22_Init (void);
void DHT22_R (uint8_t * rx);

#endif
