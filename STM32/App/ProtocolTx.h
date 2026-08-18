#ifndef PROTOCOL_TX_H
#define PROTOCOL_TX_H

#include <stdint.h>

/* 所有下行协议帧按完整行入队，避免多个任务同时写串口造成半行交错。 */
#define PROTOCOL_TX_LINE_SIZE (256U)

void ProtocolTx_Init(void);
uint8_t ProtocolTx_SendString(const char *text);
uint8_t ProtocolTx_Printf(const char *format, ...);
void ProtocolTx_RunSerialTask(void);

#endif
