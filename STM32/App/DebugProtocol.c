#include "DebugProtocol.h"
#include "DriveControl.h"
#include "Serial.h"

#define DEBUG_PROTOCOL_LINE_SIZE (128U)
#define DEBUG_PROTOCOL_MAX_TOKENS (12U)

static char g_line[DEBUG_PROTOCOL_LINE_SIZE];
static uint8_t g_lineLength;
static uint8_t g_lineOverflow;

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

static void DebugProtocol_NormalizeLine(void)
{
	uint8_t index;
	for (index = 0U; g_line[index] != '\0'; index++)
	{
		if ((g_line[index] >= 'a') && (g_line[index] <= 'z'))
		{
			g_line[index] = (char)(g_line[index] - ('a' - 'A'));
		}
		else if ((g_line[index] == ',') || (g_line[index] == '=') ||
				 (g_line[index] == '\t'))
		{
			g_line[index] = ' ';
		}
	}
}

static uint8_t DebugProtocol_Tokenize(char *tokens[], uint8_t maxTokens)
{
	char *cursor = g_line;
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
	int32_t result = 0;
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
		result = result * 10 + (int32_t)(*text - '0');
		text++;
	}
	if ((hasDigit == 0U) || (*text != '\0'))
	{
		return 0U;
	}
	*value = (negative != 0U) ? -result : result;
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

static void DebugProtocol_SendFixed6(float value)
{
	int32_t integerPart;
	int32_t fractionalPart;

	if (value < 0.0f)
	{
		Serial_SendString("-");
		value = -value;
	}
	integerPart = (int32_t)value;
	fractionalPart = (int32_t)((value - (float)integerPart) * 1000000.0f + 0.5f);
	if (fractionalPart >= 1000000)
	{
		integerPart++;
		fractionalPart -= 1000000;
	}
	Serial_Printf("%ld.%06ld", (long)integerPart, (long)fractionalPart);
}

static void DebugProtocol_SendOk(const char *command)
{
	Serial_Printf("OK C=%s\r\n", (char *)command);
}

static void DebugProtocol_SendErr(const char *command, const char *message)
{
	Serial_Printf("ERR C=%s,M=%s\r\n", (char *)command, (char *)message);
}

void DebugProtocol_SendState(void)
{
	DriveControl_Snapshot snapshot;
	char bits[9];

	DriveControl_GetSnapshot(&snapshot);
	DriveControl_FormatSensorBits(bits);

	Serial_Printf(
		"S R=%d,M=%d,SP=%d,KP=",
		snapshot.running,
		(int)snapshot.mode,
		snapshot.speed);
	DebugProtocol_SendFixed6(DriveControl_GetTrackingKp());
	Serial_SendString(",KI=");
	DebugProtocol_SendFixed6(DriveControl_GetTrackingKi());
	Serial_SendString(",KD=");
	DebugProtocol_SendFixed6(DriveControl_GetTrackingKd());
	Serial_Printf(
		",L=%d,W1=%d,W2=%d,W3=%d,W4=%d,W5=%d,W6=%d,W7=%d,W8=%d,EC=%d,EKP=",
		DriveControl_GetTrackingLimit(),
		DriveControl_GetWeight(1U),
		DriveControl_GetWeight(2U),
		DriveControl_GetWeight(3U),
		DriveControl_GetWeight(4U),
		DriveControl_GetWeight(5U),
		DriveControl_GetWeight(6U),
		DriveControl_GetWeight(7U),
		DriveControl_GetWeight(8U),
		DriveControl_GetEncoderClosed());
	DebugProtocol_SendFixed6(DriveControl_GetEncoderKp());
	Serial_SendString(",EKI=");
	DebugProtocol_SendFixed6(DriveControl_GetEncoderKi());
	Serial_Printf(
		",EFS=%ld,ECL=%d,ESE=%d,ESKP=",
		(long)DriveControl_GetEncoderFullScaleCps(),
		DriveControl_GetEncoderLimit(),
		DriveControl_GetEncoderSyncEnabled());
	DebugProtocol_SendFixed6(DriveControl_GetEncoderSyncKp());
	Serial_Printf(
		",EST=%ld,ESL=%d,NB=%d,NS=%d,S=%s,SENS=%s,E=%d,PL=%d,PR=%d,",
		(long)DriveControl_GetEncoderSyncToleranceCps(),
		DriveControl_GetEncoderSyncLimit(),
		snapshot.speed,
		snapshot.trackingState,
		bits,
		bits,
		snapshot.trackingError,
		snapshot.appliedLeftPwm,
		snapshot.appliedRightPwm);
	Serial_Printf(
		"EL=%ld,ER=%ld,TL=%ld,TR=%ld,",
		(long)snapshot.leftMeasuredCps,
		(long)snapshot.rightMeasuredCps,
		(long)snapshot.leftTargetCps,
		(long)snapshot.rightTargetCps);
	Serial_Printf(
		"VLF=%ld,VLR=%ld,VRF=%ld,VRR=%ld,ED=%ld,ESC=%d,ESA=%d,AR=0,AS=0\r\n",
		(long)snapshot.leftFrontCps,
		(long)snapshot.leftRearCps,
		(long)snapshot.rightFrontCps,
		(long)snapshot.rightRearCps,
		(long)snapshot.encoderSyncError,
		snapshot.encoderSyncCorrection,
		snapshot.encoderSyncActive);
}

void DebugProtocol_SendTelemetry(void)
{
	DriveControl_Snapshot snapshot;
	char bits[9];

	DriveControl_GetSnapshot(&snapshot);
	DriveControl_FormatSensorBits(bits);

	Serial_Printf(
		"T R=%d,M=%d,S=%s,E=%d,NB=%d,NS=%d,PL=%d,PR=%d,",
		snapshot.running,
		(int)snapshot.mode,
		bits,
		snapshot.trackingError,
		snapshot.speed,
		snapshot.trackingState,
		snapshot.appliedLeftPwm,
		snapshot.appliedRightPwm);
	Serial_Printf(
		"EL=%ld,ER=%ld,EC=%d,TL=%ld,TR=%ld,",
		(long)snapshot.leftMeasuredCps,
		(long)snapshot.rightMeasuredCps,
		snapshot.encoderClosed,
		(long)snapshot.leftTargetCps,
		(long)snapshot.rightTargetCps);
	Serial_Printf(
		"VLF=%ld,VLR=%ld,VRF=%ld,VRR=%ld,ED=%ld,ESC=%d,ESA=%d\r\n",
		(long)snapshot.leftFrontCps,
		(long)snapshot.leftRearCps,
		(long)snapshot.rightFrontCps,
		(long)snapshot.rightRearCps,
		(long)snapshot.encoderSyncError,
		snapshot.encoderSyncCorrection,
		snapshot.encoderSyncActive);
}

static void DebugProtocol_ProcessTokens(char *tokens[], uint8_t count)
{
	int32_t ivalue;
	int32_t syncTolerance;
	int32_t syncLimit;
	float kp;
	float ki;
	float kd;
	int16_t weights[DRIVE_CONTROL_SENSOR_COUNT];
	uint8_t index;

	if (count == 0U)
	{
		return;
	}

	if (DebugProtocol_StringEqual(tokens[0], "START") != 0U)
	{
		DriveControl_Start();
		DebugProtocol_SendOk("START");
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "STOP") != 0U)
	{
		DriveControl_Stop();
		DebugProtocol_SendOk("STOP");
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "RESET") != 0U)
	{
		DriveControl_Reset();
		DebugProtocol_SendOk("RESET");
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "DEFAULTS") != 0U)
	{
		DriveControl_LoadDefaults();
		DebugProtocol_SendOk("DEFAULTS");
		return;
	}
	if ((DebugProtocol_StringEqual(tokens[0], "GET") != 0U) &&
		(count >= 2U) && (DebugProtocol_StringEqual(tokens[1], "ALL") != 0U))
	{
		DebugProtocol_SendState();
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "MODE") != 0U)
	{
		if (count < 2U)
		{
			DebugProtocol_SendErr("MODE", "ARG");
		}
		else if (DebugProtocol_StringEqual(tokens[1], "TRACK") != 0U)
		{
			(void)DriveControl_SetMode(DRIVE_MODE_TRACK);
			DebugProtocol_SendOk("MODE");
		}
		else if (DebugProtocol_StringEqual(tokens[1], "STRAIGHT") != 0U)
		{
			(void)DriveControl_SetMode(DRIVE_MODE_STRAIGHT);
			DebugProtocol_SendOk("MODE");
		}
		else if (DebugProtocol_StringEqual(tokens[1], "ANGLE") != 0U)
		{
			DebugProtocol_SendErr("MODE", "ANGLE_NOT_SUPPORTED");
		}
		else
		{
			DebugProtocol_SendErr("MODE", "ARG");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "ANGLE") != 0U)
	{
		DebugProtocol_SendErr("ANGLE", "NOT_SUPPORTED");
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "SPEED") != 0U)
	{
		if ((count < 2U) || (DebugProtocol_ParseInt(tokens[1], &ivalue) == 0U) ||
			(DriveControl_SetSpeed((int16_t)ivalue) == 0U))
		{
			DebugProtocol_SendErr("SPEED", "ARG");
		}
		else
		{
			DebugProtocol_SendOk("SPEED");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "PID") != 0U)
	{
		if ((count < 4U) ||
			(DebugProtocol_ParseFloat(tokens[1], &kp) == 0U) ||
			(DebugProtocol_ParseFloat(tokens[2], &ki) == 0U) ||
			(DebugProtocol_ParseFloat(tokens[3], &kd) == 0U) ||
			(DriveControl_SetTrackingGains(kp, ki, kd) == 0U))
		{
			DebugProtocol_SendErr("PID", "ARG");
		}
		else
		{
			DebugProtocol_SendOk("PID");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "LIMIT") != 0U)
	{
		if ((count < 2U) || (DebugProtocol_ParseInt(tokens[1], &ivalue) == 0U) ||
			(DriveControl_SetTrackingLimit((int16_t)ivalue) == 0U))
		{
			DebugProtocol_SendErr("LIMIT", "ARG");
		}
		else
		{
			DebugProtocol_SendOk("LIMIT");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "WEIGHT") != 0U)
	{
		int32_t channel;
		int32_t weight;
		if ((count < 3U) ||
			(DebugProtocol_ParseInt(tokens[1], &channel) == 0U) ||
			(DebugProtocol_ParseInt(tokens[2], &weight) == 0U) ||
			(DriveControl_SetWeight((uint8_t)channel, (int16_t)weight) == 0U))
		{
			DebugProtocol_SendErr("WEIGHT", "ARG");
		}
		else
		{
			DebugProtocol_SendOk("WEIGHT");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "WEIGHTS") != 0U)
	{
		if (count < 9U)
		{
			DebugProtocol_SendErr("WEIGHTS", "ARG");
			return;
		}
		for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
		{
			if (DebugProtocol_ParseInt(tokens[index + 1U], &ivalue) == 0U)
			{
				DebugProtocol_SendErr("WEIGHTS", "ARG");
				return;
			}
			weights[index] = (int16_t)ivalue;
		}
		if (DriveControl_SetWeights(weights) == 0U)
		{
			DebugProtocol_SendErr("WEIGHTS", "ARG");
		}
		else
		{
			DebugProtocol_SendOk("WEIGHTS");
		}
		return;
	}
	if (DebugProtocol_StringEqual(tokens[0], "ENC") != 0U)
	{
		if (count < 2U)
		{
			DebugProtocol_SendErr("ENC", "ARG");
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "ON") != 0U)
		{
			DriveControl_SetEncoderClosed(1U);
			DebugProtocol_SendOk("ENC");
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "OFF") != 0U)
		{
			DriveControl_SetEncoderClosed(0U);
			DebugProtocol_SendOk("ENC");
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "PID") != 0U)
		{
			if ((count < 4U) ||
				(DebugProtocol_ParseFloat(tokens[2], &kp) == 0U) ||
				(DebugProtocol_ParseFloat(tokens[3], &ki) == 0U) ||
				(DriveControl_SetEncoderGains(kp, ki) == 0U))
			{
				DebugProtocol_SendErr("ENC", "ARG");
			}
			else
			{
				DebugProtocol_SendOk("ENC");
			}
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "CPS") != 0U)
		{
			if ((count < 3U) || (DebugProtocol_ParseInt(tokens[2], &ivalue) == 0U) ||
				(DriveControl_SetEncoderFullScaleCps(ivalue) == 0U))
			{
				DebugProtocol_SendErr("ENC", "ARG");
			}
			else
			{
				DebugProtocol_SendOk("ENC");
			}
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "LIMIT") != 0U)
		{
			if ((count < 3U) || (DebugProtocol_ParseInt(tokens[2], &ivalue) == 0U) ||
				(DriveControl_SetEncoderLimit((int16_t)ivalue) == 0U))
			{
				DebugProtocol_SendErr("ENC", "ARG");
			}
			else
			{
				DebugProtocol_SendOk("ENC");
			}
			return;
		}
		if (DebugProtocol_StringEqual(tokens[1], "SYNC") != 0U)
		{
			if ((count >= 3U) && (DebugProtocol_StringEqual(tokens[2], "ON") != 0U))
			{
				DriveControl_SetEncoderSyncEnabled(1U);
				DebugProtocol_SendOk("ENC");
				return;
			}
			if ((count >= 3U) && (DebugProtocol_StringEqual(tokens[2], "OFF") != 0U))
			{
				DriveControl_SetEncoderSyncEnabled(0U);
				DebugProtocol_SendOk("ENC");
				return;
			}
			if ((count < 5U) ||
				(DebugProtocol_ParseFloat(tokens[2], &kp) == 0U) ||
				(DebugProtocol_ParseInt(tokens[3], &syncTolerance) == 0U) ||
				(DebugProtocol_ParseInt(tokens[4], &syncLimit) == 0U) ||
				(syncLimit > DRIVE_CONTROL_PWM_MAX) ||
				(DriveControl_SetEncoderSync(kp, syncTolerance, (int16_t)syncLimit) == 0U))
			{
				DebugProtocol_SendErr("ENC", "ARG");
			}
			else
			{
				DebugProtocol_SendOk("ENC");
			}
			return;
		}
		DebugProtocol_SendErr("ENC", "ARG");
		return;
	}

	DebugProtocol_SendErr(tokens[0], "UNKNOWN");
}

static void DebugProtocol_ProcessLine(void)
{
	char *tokens[DEBUG_PROTOCOL_MAX_TOKENS];
	uint8_t count;

	DebugProtocol_NormalizeLine();
	count = DebugProtocol_Tokenize(tokens, DEBUG_PROTOCOL_MAX_TOKENS);
	DebugProtocol_ProcessTokens(tokens, count);
}

void DebugProtocol_Init(void)
{
	g_lineLength = 0U;
	g_lineOverflow = 0U;
	g_line[0] = '\0';
}

void DebugProtocol_Run(void)
{
	uint8_t byte;

	while (Serial_Available() != 0U)
	{
		byte = Serial_ReadByte();
		if ((byte == (uint8_t)'\r') || (byte == (uint8_t)'\n'))
		{
			if ((g_lineOverflow == 0U) && (g_lineLength != 0U))
			{
				g_line[g_lineLength] = '\0';
				DebugProtocol_ProcessLine();
			}
			g_lineLength = 0U;
			g_lineOverflow = 0U;
		}
		else if ((byte >= 0x20U) && (byte <= 0x7EU))
		{
			if (g_lineLength < (DEBUG_PROTOCOL_LINE_SIZE - 1U))
			{
				g_line[g_lineLength++] = (char)byte;
			}
			else
			{
				g_lineOverflow = 1U;
			}
		}
	}
}
