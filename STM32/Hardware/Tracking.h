#ifndef TRACKING_H
#define TRACKING_H

#include <stdint.h>

#define TRACKING_CHANNEL_COUNT      (8U)
#define TRACKING_ADDRESS_SETTLE_US  (1U)

void Tracking_Init(void);
uint8_t Tracking_ReadAll(void);

#endif
