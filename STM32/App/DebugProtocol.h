#ifndef __DEBUG_PROTOCOL_H
#define __DEBUG_PROTOCOL_H

#include "Serial.h"

/* 初始化两个端口的 RX 行缓存；串口硬件初始化在 Serial_Init 中完成。 */
void DebugProtocol_Init(void);
/* 从指定端口取字节并解析完整命令行，由 ProtocolTask 逐端口调用。 */
void DebugProtocol_Run(Serial_Port port);
/* 向指定命令来源发送 GET ALL 的 S 和 CFG 两行响应。 */
void DebugProtocol_SendState(Serial_Port port);
/* 启动时向两个端口广播 S 和 CFG。 */
void DebugProtocol_BroadcastState(void);
/* 向两个端口广播周期遥测 T 帧。 */
void DebugProtocol_SendTelemetry(void);
/* 发送命令确认/拒绝回执，command 对应上位机等待的 C 字段。 */
void DebugProtocol_SendOk(Serial_Port port, const char *command);
void DebugProtocol_SendErr(Serial_Port port, const char *command,
						   const char *message);

#endif
