#ifndef UGV_COMMAND_H
#define UGV_COMMAND_H

#include <stdint.h>

typedef enum
{
	UGV_COMMAND_START = 0,
	UGV_COMMAND_STOP,
	UGV_COMMAND_RESET,
	UGV_COMMAND_DEFAULTS,
	UGV_COMMAND_PING,
	UGV_COMMAND_GET_ALL,
	UGV_COMMAND_FAULT_CLEAR,
	UGV_COMMAND_MODE,
	UGV_COMMAND_WHEEL_PWM,
	UGV_COMMAND_SPEED,
	UGV_COMMAND_ENCODER_ENABLE,
	UGV_COMMAND_ENCODER_GAINS,
	UGV_COMMAND_ENCODER_FULL_SCALE,
	UGV_COMMAND_ENCODER_LIMIT,
	UGV_COMMAND_ENCODER_SYNC_ENABLE,
	UGV_COMMAND_ENCODER_SYNC_PARAMS
} UgvCommand_Type;

/* 协议任务只解析文本并投递该结构；实际改车状态统一由 ControlTask 完成。 */
typedef struct
{
	UgvCommand_Type type;
	char responseName[16];
	int32_t first;
	int32_t second;
	int32_t third;
	float kp;
	float ki;
	uint8_t enabled;
} UgvCommand;

#endif
