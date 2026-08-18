#ifndef __DEBUG_PROTOCOL_H
#define __DEBUG_PROTOCOL_H

#include "ICM42688.h"

void DebugProtocol_Init(void);
void DebugProtocol_Run(void);
void DebugProtocol_SendState(void);
void DebugProtocol_SendTelemetry(void);
void DebugProtocol_SendImuRaw(const ICM42688_RawSample *sample);
void DebugProtocol_SendOk(const char *command);
void DebugProtocol_SendErr(const char *command, const char *message);

#endif
