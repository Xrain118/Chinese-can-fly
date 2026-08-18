#ifndef UGV_COMMAND_QUEUE_H
#define UGV_COMMAND_QUEUE_H

#include "UgvCommand.h"
#include <stdint.h>

void UgvCommandQueue_Init(void);
uint8_t UgvCommandQueue_Send(const UgvCommand *command);
uint8_t UgvCommandQueue_Receive(UgvCommand *command, uint32_t waitMs);

#endif
