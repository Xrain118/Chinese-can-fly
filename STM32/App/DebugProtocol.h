#ifndef __DEBUG_PROTOCOL_H
#define __DEBUG_PROTOCOL_H

#include "ICM42688.h"

/* 初始化 RX 行缓存；串口硬件初始化在 Serial_Init 中完成。 */
void DebugProtocol_Init(void);
/* 从 Serial 环形缓冲取字节并解析完整命令行，通常由 ProtocolTask 周期调用。 */
void DebugProtocol_Run(void);
/* 发送 GET ALL 响应：一行运行状态 S，加一行配置 CFG。 */
void DebugProtocol_SendState(void);
/* 发送周期遥测 T 帧。 */
void DebugProtocol_SendTelemetry(void);
/* 发送原始 IMU I 帧，字段仍是芯片原始计数。 */
void DebugProtocol_SendImuRaw(const ICM42688_RawSample *sample);
/* 发送命令确认/拒绝回执，command 对应上位机等待的 C 字段。 */
void DebugProtocol_SendOk(const char *command);
void DebugProtocol_SendErr(const char *command, const char *message);

#endif
