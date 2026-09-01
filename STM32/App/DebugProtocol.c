/*
 * STM32 文本调试协议。
 *
 * 串口 RX 在这里组行、归一化、解析命令，再投递到 UgvCommandQueue；
 * 这里不直接改车状态，避免协议任务和控制任务同时写 DriveControl。
 * 下行的 OK/ERR/T/S/CFG/I 帧统一进入 ProtocolTx 队列，由串口发送任务串行输出。
 */
#include "DebugProtocol.h"
#include "DriveControl.h"
#include "ProtocolTx.h"
#include "Serial.h"
#include "UgvCommandQueue.h"
#include <stdarg.h>
#include <stdio.h>

#define DEBUG_PROTOCOL_LINE_SIZE (128U)
#define DEBUG_PROTOCOL_MAX_TOKENS (8U)

typedef struct
{
	char text[PROTOCOL_TX_LINE_SIZE];
	uint16_t length;
	uint8_t truncated;
} DebugProtocol_LineBuilder;

typedef struct
{
	const char *name;
	UgvCommand_Type type;
} DebugProtocol_SimpleCommand;

typedef struct
{
	char line[DEBUG_PROTOCOL_LINE_SIZE];
	uint8_t lineLength;
	uint8_t lineOverflow;
} DebugProtocol_RxContext;

/* 无参数命令使用声明式映射，新增同类命令时不必再复制一整段 if 分支。 */
static const DebugProtocol_SimpleCommand g_simpleCommands[] =
{
	{"START", UGV_COMMAND_START},
	{"STOP", UGV_COMMAND_STOP},
	{"RESET", UGV_COMMAND_RESET},
	{"DEFAULTS", UGV_COMMAND_DEFAULTS}
};

/* 两个端口分别组行；协议任务串行处理时用 active 指针复用解析函数。 */
static DebugProtocol_RxContext g_rxContext[SERIAL_PORT_COUNT];
static DebugProtocol_RxContext *g_activeContext;
static Serial_Port g_activePort;

static uint8_t DebugProtocol_StringEqual(const char *left, const char *right)
{
	while ((*left != '\0') && (*right != '\0'))
	{
		if (*left != *right)
		{
			return 0U;
		}
		left++;
		right++;
	}
	return ((*left == '\0') && (*right == '\0')) ? 1U : 0U;
}

static void DebugProtocol_CopyCommandName(char *target, uint8_t targetSize,
										  const char *source)
{
	uint8_t index = 0U;

	if ((target == 0) || (targetSize == 0U))
	{
		return;
	}
	if (source != 0)
	{
		while ((source[index] != '\0') && (index < (uint8_t)(targetSize - 1U)))
		{
			target[index] = source[index];
			index++;
		}
	}
	target[index] = '\0';
}

static void DebugProtocol_InitCommand(UgvCommand *command,
									  const char *responseName)
{
	if (command == 0)
	{
		return;
	}

	/* 显式清空所有通用参数槽，避免不同命令复用结构时携带无关旧值。 */
	command->type = UGV_COMMAND_START;
	command->sourcePort = g_activePort;
	command->responseName[0] = '\0';
	command->first = 0;
	command->second = 0;
	command->third = 0;
	command->kp = 0.0f;
	command->ki = 0.0f;
	command->enabled = 0U;
	DebugProtocol_CopyCommandName(command->responseName,
								  sizeof(command->responseName),
								  responseName);
}

static void DebugProtocol_NormalizeLine(void)
{
	uint8_t index;
	for (index = 0U; g_activeContext->line[index] != '\0'; index++)
	{
		/* 协议对大小写不敏感；把逗号/等号也当分隔符，兼容 KEY=VALUE 和空格命令。 */
		if ((g_activeContext->line[index] >= 'a') &&
			(g_activeContext->line[index] <= 'z'))
		{
			g_activeContext->line[index] =
				(char)(g_activeContext->line[index] - ('a' - 'A'));
		}
		else if ((g_activeContext->line[index] == ',') ||
				 (g_activeContext->line[index] == '=') ||
				 (g_activeContext->line[index] == '\t'))
		{
			g_activeContext->line[index] = ' ';
		}
	}
}

static uint8_t DebugProtocol_Tokenize(char *tokens[], uint8_t maxTokens)
{
	char *cursor = g_activeContext->line;
	uint8_t count = 0U;

	while (*cursor != '\0')
	{
		while (*cursor == ' ')
		{
			cursor++;
		}
		if (*cursor == '\0')
		{
			break;
		}
		if (count >= maxTokens)
		{
			return count;
		}
		tokens[count++] = cursor;
		while ((*cursor != '\0') && (*cursor != ' '))
		{
			cursor++;
		}
		if (*cursor != '\0')
		{
			*cursor++ = '\0';
		}
	}
	return count;
}

static uint8_t DebugProtocol_ParseInt(const char *text, int32_t *value)
{
	uint32_t result = 0U;
	uint32_t limit;
	uint32_t digit;
	uint8_t negative = 0U;
	uint8_t hasDigit = 0U;
	if ((text == 0) || (value == 0))
	{
		return 0U;
	}
	if ((*text == '+') || (*text == '-'))
	{
		negative = (*text == '-') ? 1U : 0U;
		text++;
	}
	/* 不用 sscanf，避免嵌入式 C 库浮动配置差异；这里手动检查 int32 溢出。 */
	limit = (negative != 0U) ? 2147483648UL : 2147483647UL;
	while ((*text >= '0') && (*text <= '9'))
	{
		hasDigit = 1U;
		digit = (uint32_t)(*text - '0');
		if (result > ((limit - digit) / 10UL))
		{
			return 0U;
		}
		result = result * 10UL + digit;
		text++;
	}
	if ((hasDigit == 0U) || (*text != '\0'))
	{
		return 0U;
	}
	if (negative != 0U)
	{
		*value = (result == 2147483648UL) ? (int32_t)0x80000000UL : -(int32_t)result;
	}
	else
	{
		*value = (int32_t)result;
	}
	return 1U;
}

static uint8_t DebugProtocol_ParseFloat(const char *text, float *value)
{
	float result = 0.0f;
	float place = 0.1f;
	uint8_t negative = 0U;
	uint8_t hasDigit = 0U;
	if ((text == 0) || (value == 0))
	{
		return 0U;
	}
	if ((*text == '+') || (*text == '-'))
	{
		negative = (*text == '-') ? 1U : 0U;
		text++;
	}
	while ((*text >= '0') && (*text <= '9'))
	{
		hasDigit = 1U;
		result = result * 10.0f + (float)(*text - '0');
		text++;
	}
	if (*text == '.')
	{
		text++;
		while ((*text >= '0') && (*text <= '9'))
		{
			hasDigit = 1U;
			result += (float)(*text - '0') * place;
			place *= 0.1f;
			text++;
		}
	}
	if ((hasDigit == 0U) || (*text != '\0'))
	{
		return 0U;
	}
	*value = (negative != 0U) ? -result : result;
	return 1U;
}

static uint8_t DebugProtocol_ParseSwitch(const char *text, uint8_t *enabled)
{
	if ((text == 0) || (enabled == 0))
	{
		return 0U;
	}
	if (DebugProtocol_StringEqual(text, "ON") != 0U)
	{
		*enabled = 1U;
		return 1U;
	}
	if (DebugProtocol_StringEqual(text, "OFF") != 0U)
	{
		*enabled = 0U;
		return 1U;
	}
	return 0U;
}

static uint8_t DebugProtocol_IsPwmValue(int32_t value)
{
	return ((value >= -DRIVE_CONTROL_PWM_MAX) &&
			(value <= DRIVE_CONTROL_PWM_MAX)) ? 1U : 0U;
}

static void DebugProtocol_LineInit(DebugProtocol_LineBuilder *builder)
{
	builder->length = 0U;
	builder->truncated = 0U;
	builder->text[0] = '\0';
}

static void DebugProtocol_LineAppend(DebugProtocol_LineBuilder *builder,
									 const char *format, ...)
{
	va_list args;
	int written;
	uint16_t remaining;

	if ((builder == 0) || (format == 0) || (builder->truncated != 0U))
	{
		return;
	}
	remaining = (uint16_t)(sizeof(builder->text) - builder->length);
	va_start(args, format);
	written = vsnprintf(&builder->text[builder->length], remaining, format, args);
	va_end(args);
	if ((written < 0) || ((uint32_t)written >= remaining))
	{
		/* 状态帧超长时整帧丢弃，避免发送缺少 CRLF 的半行。 */
		builder->truncated = 1U;
		builder->text[sizeof(builder->text) - 1U] = '\0';
		return;
	}
	builder->length = (uint16_t)(builder->length + (uint16_t)written);
}

static void DebugProtocol_LineAppendFixed6(DebugProtocol_LineBuilder *builder,
										  float value)
{
	int32_t integerPart;
	int32_t fractionalPart;

	/* 固件侧不依赖 printf 的 %f 支持，浮点参数统一手动格式化为 6 位小数。 */
	if (value < 0.0f)
	{
		DebugProtocol_LineAppend(builder, "-");
		value = -value;
	}
	integerPart = (int32_t)value;
	fractionalPart = (int32_t)((value - (float)integerPart) * 1000000.0f + 0.5f);
	if (fractionalPart >= 1000000)
	{
		integerPart++;
		fractionalPart -= 1000000;
	}
	DebugProtocol_LineAppend(builder, "%ld.%06ld",
							 (long)integerPart, (long)fractionalPart);
}

static void DebugProtocol_LineSend(Serial_Port port,
								   DebugProtocol_LineBuilder *builder)
{
	if ((builder == 0) || (builder->truncated != 0U))
	{
		return;
	}
	(void)ProtocolTx_SendString(port, builder->text);
}

static void DebugProtocol_LineBroadcast(DebugProtocol_LineBuilder *builder)
{
	if ((builder == 0) || (builder->truncated != 0U))
	{
		return;
	}
	(void)ProtocolTx_BroadcastString(builder->text);
}

void DebugProtocol_SendOk(Serial_Port port, const char *command)
{
	(void)ProtocolTx_Printf(port, "OK C=%s\r\n",
							(command == 0) ? "" : command);
}

void DebugProtocol_SendErr(Serial_Port port, const char *command,
							   const char *message)
{
	(void)ProtocolTx_Printf(port, "ERR C=%s,M=%s\r\n",
							(command == 0) ? "" : command,
							(message == 0) ? "" : message);
}

static void DebugProtocol_BuildDriveLine(const char *prefix,
									 DebugProtocol_LineBuilder *line)
{
	DriveControl_Snapshot snapshot;

	DriveControl_GetSnapshot(&snapshot);
	DebugProtocol_LineInit(line);
	/* T/S 共享运行快照字段；配置参数放到独立 CFG 帧，方便前端区分同步完成点。 */
	DebugProtocol_LineAppend(
		line,
		"%s R=%d,M=%d,SP=%d,DL=%d,DR=%d,PL=%d,PR=%d,"
		"EL=%ld,ER=%ld,EC=%d,TL=%ld,TR=%ld,"
		"VLF=%ld,VLR=%ld,VRF=%ld,VRR=%ld,ED=%ld,ESC=%d,ESA=%d\r\n",
		(char *)prefix,
		snapshot.running,
		(int)snapshot.mode,
		snapshot.speed,
		snapshot.desiredLeftPwm,
		snapshot.desiredRightPwm,
		snapshot.appliedLeftPwm,
		snapshot.appliedRightPwm,
		(long)snapshot.leftMeasuredCps,
		(long)snapshot.rightMeasuredCps,
		snapshot.encoderClosed,
		(long)snapshot.leftTargetCps,
		(long)snapshot.rightTargetCps,
		(long)snapshot.leftFrontCps,
		(long)snapshot.leftRearCps,
		(long)snapshot.rightFrontCps,
		(long)snapshot.rightRearCps,
		(long)snapshot.encoderSyncError,
		snapshot.encoderSyncCorrection,
		snapshot.encoderSyncActive);
}

static void DebugProtocol_BuildConfigLine(DebugProtocol_LineBuilder *line)
{
	DebugProtocol_LineInit(line);
	/* GET ALL 返回 S + CFG 两行；前端收到 CFG 后才认为配置已同步。 */
	DebugProtocol_LineAppend(line, "CFG EC=%d,EKP=",
								 DriveControl_GetEncoderClosed());
	DebugProtocol_LineAppendFixed6(line, DriveControl_GetEncoderKp());
	DebugProtocol_LineAppend(line, ",EKI=");
	DebugProtocol_LineAppendFixed6(line, DriveControl_GetEncoderKi());
	DebugProtocol_LineAppend(line, ",EFS=%ld,ECL=%d,ESE=%d,ESKP=",
								 (long)DriveControl_GetEncoderFullScaleCps(),
								 DriveControl_GetEncoderLimit(),
								 DriveControl_GetEncoderSyncEnabled());
	DebugProtocol_LineAppendFixed6(line, DriveControl_GetEncoderSyncKp());
	DebugProtocol_LineAppend(line, ",EST=%ld,ESL=%d\r\n",
									 (long)DriveControl_GetEncoderSyncToleranceCps(),
									 DriveControl_GetEncoderSyncLimit());
}

void DebugProtocol_SendState(Serial_Port port)
{
	DebugProtocol_LineBuilder line;

	DebugProtocol_BuildDriveLine("S", &line);
	DebugProtocol_LineSend(port, &line);
	DebugProtocol_BuildConfigLine(&line);
	DebugProtocol_LineSend(port, &line);
}

void DebugProtocol_BroadcastState(void)
{
	DebugProtocol_LineBuilder line;

	DebugProtocol_BuildDriveLine("S", &line);
	DebugProtocol_LineBroadcast(&line);
	DebugProtocol_BuildConfigLine(&line);
	DebugProtocol_LineBroadcast(&line);
}

void DebugProtocol_SendTelemetry(void)
{
	DebugProtocol_LineBuilder line;

	DebugProtocol_BuildDriveLine("T", &line);
	DebugProtocol_LineBroadcast(&line);
}

static void DebugProtocol_SubmitCommand(const UgvCommand *command)
{
	if (command == 0)
	{
		return;
	}
	if (UgvCommandQueue_Send(command) == 0U)
	{
		/* 队列满说明控制任务暂时跟不上，直接拒绝本条命令而不是阻塞协议任务。 */
		DebugProtocol_SendErr(command->sourcePort, command->responseName, "BUSY");
	}
}

static void DebugProtocol_ProcessEncoderSyncCommand(UgvCommand *command,
												 char *tokens[],
												 uint8_t count)
{
	int32_t syncTolerance;
	int32_t syncLimit;
	float kp;

	/* ENC SYNC 既有开关子命令，也有参数子命令，必须先尝试识别 ON/OFF。 */
	if ((count >= 3U) &&
		(DebugProtocol_ParseSwitch(tokens[2], &command->enabled) != 0U))
	{
		command->type = UGV_COMMAND_ENCODER_SYNC_ENABLE;
		DebugProtocol_SubmitCommand(command);
		return;
	}
	if ((count < 5U) ||
		(DebugProtocol_ParseFloat(tokens[2], &kp) == 0U) ||
		(DebugProtocol_ParseInt(tokens[3], &syncTolerance) == 0U) ||
		(DebugProtocol_ParseInt(tokens[4], &syncLimit) == 0U) ||
		(syncLimit < 0) || (syncLimit > DRIVE_CONTROL_PWM_MAX))
	{
		DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
		return;
	}

	command->type = UGV_COMMAND_ENCODER_SYNC_PARAMS;
	command->kp = kp;
	command->first = syncTolerance;
	command->second = syncLimit;
	DebugProtocol_SubmitCommand(command);
}

static void DebugProtocol_ProcessEncoderCommand(char *tokens[], uint8_t count)
{
	UgvCommand command;
	int32_t ivalue;
	float kp;
	float ki;

	if (count < 2U)
	{
		DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
		return;
	}
	DebugProtocol_InitCommand(&command, "ENC");
	if (DebugProtocol_ParseSwitch(tokens[1], &command.enabled) != 0U)
	{
		command.type = UGV_COMMAND_ENCODER_ENABLE;
		DebugProtocol_SubmitCommand(&command);
		return;
	}
	if (DebugProtocol_StringEqual(tokens[1], "PID") != 0U)
	{
		if ((count < 4U) ||
			(DebugProtocol_ParseFloat(tokens[2], &kp) == 0U) ||
			(DebugProtocol_ParseFloat(tokens[3], &ki) == 0U))
		{
			DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
		}
		else
		{
			command.type = UGV_COMMAND_ENCODER_GAINS;
			command.kp = kp;
			command.ki = ki;
			DebugProtocol_SubmitCommand(&command);
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[1], "CPS") != 0U)
	{
		if ((count < 3U) || (DebugProtocol_ParseInt(tokens[2], &ivalue) == 0U))
		{
			DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
		}
		else
		{
			command.type = UGV_COMMAND_ENCODER_FULL_SCALE;
			command.first = ivalue;
			DebugProtocol_SubmitCommand(&command);
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[1], "LIMIT") != 0U)
	{
		if ((count < 3U) || (DebugProtocol_ParseInt(tokens[2], &ivalue) == 0U) ||
			(ivalue < 0) || (ivalue > DRIVE_CONTROL_PWM_MAX))
		{
			DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
		}
		else
		{
			command.type = UGV_COMMAND_ENCODER_LIMIT;
			command.first = ivalue;
			DebugProtocol_SubmitCommand(&command);
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[1], "SYNC") != 0U)
	{
		DebugProtocol_ProcessEncoderSyncCommand(&command, tokens, count);
		return;
	}
	DebugProtocol_SendErr(g_activePort, "ENC", "ARG");
}

static uint8_t DebugProtocol_TrySubmitSimpleCommand(const char *name,
												 UgvCommand *command)
{
	uint8_t index;
	uint8_t commandCount =
		(uint8_t)(sizeof(g_simpleCommands) / sizeof(g_simpleCommands[0]));

	for (index = 0U; index < commandCount; index++)
	{
		if (DebugProtocol_StringEqual(name, g_simpleCommands[index].name) != 0U)
		{
			command->type = g_simpleCommands[index].type;
			DebugProtocol_SubmitCommand(command);
			return 1U;
		}
	}
	return 0U;
}

static void DebugProtocol_ProcessModeCommand(UgvCommand *command,
											 char *tokens[],
											 uint8_t count)
{
	if (count < 2U)
	{
		DebugProtocol_SendErr(g_activePort, "MODE", "ARG");
		return;
	}
	if (DebugProtocol_StringEqual(tokens[1], "DIRECT") != 0U)
	{
		command->first = DRIVE_MODE_DIRECT;
	}
	else if (DebugProtocol_StringEqual(tokens[1], "STRAIGHT") != 0U)
	{
		command->first = DRIVE_MODE_STRAIGHT;
	}
	else
	{
		DebugProtocol_SendErr(g_activePort, "MODE", "ARG");
		return;
	}

	command->type = UGV_COMMAND_MODE;
	DebugProtocol_SubmitCommand(command);
}

static void DebugProtocol_ProcessWheelPwmCommand(UgvCommand *command,
												 char *tokens[],
												 uint8_t count)
{
	int32_t left;
	int32_t right;

	if ((count < 3U) ||
		(DebugProtocol_ParseInt(tokens[1], &left) == 0U) ||
		(DebugProtocol_ParseInt(tokens[2], &right) == 0U) ||
		(DebugProtocol_IsPwmValue(left) == 0U) ||
		(DebugProtocol_IsPwmValue(right) == 0U))
	{
		DebugProtocol_SendErr(g_activePort, tokens[0], "ARG");
		return;
	}

	command->type = UGV_COMMAND_WHEEL_PWM;
	command->first = left;
	command->second = right;
	DebugProtocol_SubmitCommand(command);
}

static void DebugProtocol_ProcessSpeedCommand(UgvCommand *command,
											  char *tokens[],
											  uint8_t count)
{
	int32_t speed;

	if ((count < 2U) || (DebugProtocol_ParseInt(tokens[1], &speed) == 0U) ||
		(DebugProtocol_IsPwmValue(speed) == 0U))
	{
		DebugProtocol_SendErr(g_activePort, "SPEED", "ARG");
		return;
	}

	command->type = UGV_COMMAND_SPEED;
	command->first = speed;
	DebugProtocol_SubmitCommand(command);
}

static void DebugProtocol_ProcessTokens(char *tokens[], uint8_t count)
{
	UgvCommand command;

	if (count == 0U)
	{
		return;
	}
	DebugProtocol_InitCommand(&command, tokens[0]);

	/* 分支顺序从无参数命令到复合命令，与协议文档中的分类保持一致。 */
	if (DebugProtocol_TrySubmitSimpleCommand(tokens[0], &command) != 0U)
	{
		return;
	}
	if ((DebugProtocol_StringEqual(tokens[0], "GET") != 0U) &&
		(count >= 2U) && (DebugProtocol_StringEqual(tokens[1], "ALL") != 0U))
	{
		command.type = UGV_COMMAND_GET_ALL;
		DebugProtocol_SubmitCommand(&command);
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "MODE") != 0U)
	{
		DebugProtocol_ProcessModeCommand(&command, tokens, count);
		return;
	}
	if ((DebugProtocol_StringEqual(tokens[0], "PWM") != 0U) ||
		(DebugProtocol_StringEqual(tokens[0], "MOVE") != 0U))
	{
		DebugProtocol_ProcessWheelPwmCommand(&command, tokens, count);
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "SPEED") != 0U)
	{
		DebugProtocol_ProcessSpeedCommand(&command, tokens, count);
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "ENC") != 0U)
	{
		DebugProtocol_ProcessEncoderCommand(tokens, count);
		return;
	}

	DebugProtocol_SendErr(g_activePort, tokens[0], "UNKNOWN");
}

static void DebugProtocol_ProcessLine(void)
{
	char *tokens[DEBUG_PROTOCOL_MAX_TOKENS];
	uint8_t count;

	/* 一行命令处理完即丢弃缓存；过长行在 DebugProtocol_Run 中整行丢弃。 */
	DebugProtocol_NormalizeLine();
	count = DebugProtocol_Tokenize(tokens, DEBUG_PROTOCOL_MAX_TOKENS);
	DebugProtocol_ProcessTokens(tokens, count);
}

void DebugProtocol_Init(void)
{
	Serial_Port port;
	for (port = SERIAL_PORT_BLUETOOTH; port < SERIAL_PORT_COUNT; port++)
	{
		g_rxContext[port].lineLength = 0U;
		g_rxContext[port].lineOverflow = 0U;
		g_rxContext[port].line[0] = '\0';
	}
	g_activePort = SERIAL_PORT_BLUETOOTH;
	g_activeContext = &g_rxContext[SERIAL_PORT_BLUETOOTH];
}

void DebugProtocol_Run(Serial_Port port)
{
	uint8_t byte;

	if ((uint32_t)port >= (uint32_t)SERIAL_PORT_COUNT)
	{
		return;
	}
	g_activePort = port;
	g_activeContext = &g_rxContext[port];
	/* 本任务每 1ms 被调度一次，尽量一次取空环形缓冲，降低命令响应延迟。 */
	while (Serial_Available(port) != 0U)
	{
		byte = Serial_ReadByte(port);
		if ((byte == (uint8_t)'\r') || (byte == (uint8_t)'\n'))
		{
			if ((g_activeContext->lineOverflow == 0U) &&
				(g_activeContext->lineLength != 0U))
			{
				g_activeContext->line[g_activeContext->lineLength] = '\0';
				DebugProtocol_ProcessLine();
			}
			g_activeContext->lineLength = 0U;
			g_activeContext->lineOverflow = 0U;
		}
		else if ((byte >= 0x20U) && (byte <= 0x7EU))
		{
			if (g_activeContext->lineLength < (DEBUG_PROTOCOL_LINE_SIZE - 1U))
			{
				g_activeContext->line[g_activeContext->lineLength++] = (char)byte;
			}
			else
			{
				g_activeContext->lineOverflow = 1U;
			}
		}
	}
}
