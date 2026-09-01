#ifndef PROTOCOL_TX_H
#define PROTOCOL_TX_H

#include "Serial.h"
#include <stdint.h>

/* 所有下行协议帧按完整行入队，避免多个任务同时写串口造成半行交错。 */
#define PROTOCOL_TX_LINE_SIZE (256U)

/* 为每个串口创建独立静态 TX 队列；任务启动前调用。 */
void ProtocolTx_Init(void);
/* 向指定端口发送完整行；返回 0 表示端口、队列或入队失败。 */
uint8_t ProtocolTx_SendString(Serial_Port port, const char *text);
/* 向指定端口格式化并发送完整行；被截断的帧不会发送。 */
uint8_t ProtocolTx_Printf(Serial_Port port, const char *format, ...);
/* 把同一完整行分别加入两个端口的队列。 */
uint8_t ProtocolTx_BroadcastString(const char *text);
/* 指定端口的发送任务主体，独占该端口的阻塞 TX，永不返回。 */
void ProtocolTx_RunSerialTask(Serial_Port port);

#endif
