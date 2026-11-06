#include "DHT22.h"

bool DHT22_Init (void) {
    OneWire_Init ();
    OneWire_Start ();
    if (OneWire_Ack ()) {
        return false;
    }
    return true;
}

void DHT22_R (uint8_t * rx) {
    for (int i = 0;i < 5;i++) {
        OneWire_R_Byte (rx + i);
    }
}