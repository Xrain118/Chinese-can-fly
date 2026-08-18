#ifndef PROTOCOL_TX_H
#define PROTOCOL_TX_H

#include <stdint.h>

/* 所有下行协议帧按完整行入队，避免多个任务同时写串口造成半行交错。 */
#define PROTOCOL_TX_LINE_SIZE (256U)

/* 创建静态 TX 队列；任务启动前调用。 */
void ProtocolTx_Init(void);
/* 发送一整行已格式化文本；返回 0 表示队列不可用或入队失败。 */
uint8_t ProtocolTx_SendString(const char *text);
/* 格式化并发送一整行；不支持被截断的帧。 */
uint8_t ProtocolTx_Printf(const char *format, ...);
/* 串口发送任务主体，独占 Serial_SendString，永不返回。 */
void ProtocolTx_RunSerialTask(void);

#endif
