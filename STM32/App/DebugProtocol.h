#ifndef __DEBUG_PROTOCOL_H
#define __DEBUG_PROTOCOL_H

void DebugProtocol_Init(void);
void DebugProtocol_Run(void);
void DebugProtocol_SendState(void);
void DebugProtocol_SendTelemetry(void);

#endif
