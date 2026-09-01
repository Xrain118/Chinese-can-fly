#ifndef UGV_COMMAND_H
#define UGV_COMMAND_H

#include "Serial.h"
#include <stdint.h>

/* 协议层解析后的命令类型；ControlTask 再根据 type 调用对应控制模块。 */
typedef enum
{
	UGV_COMMAND_START = 0,
	UGV_COMMAND_STOP,
	UGV_COMMAND_RESET,
	UGV_COMMAND_DEFAULTS,
	UGV_COMMAND_GET_ALL,
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
	/* 命令来自哪个物理串口；ACK、ERR 和查询结果只返回该端口。 */
	Serial_Port sourcePort;
	/* OK/ERR 回包中的 C 字段名称，保持和上位机等待的命令名一致。 */
	char responseName[16];
	/* 通用整数参数槽：左右 PWM、模式值、CPS、限幅等都复用这里。 */
	int32_t first;
	int32_t second;
	int32_t third;
	/* 浮点参数槽：当前用于速度 PI 和同步 P。 */
	float kp;
	float ki;
	/* 开关类命令统一使用 enabled。 */
	uint8_t enabled;
} UgvCommand;

#endif
