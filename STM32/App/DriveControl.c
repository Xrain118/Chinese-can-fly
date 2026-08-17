#include "DriveControl.h"
#include "SimplePID.h"
#include "Encoder.h"

#if DRIVE_CONTROL_ENABLE_HARDWARE
#include "Motor.h"
#include "Tracking.h"
#endif

typedef struct
{
	SimplePID trackingPid;
	SimplePID leftSpeedPid;
	SimplePID rightSpeedPid;
	DriveControl_Snapshot snapshot;
	int16_t weights[DRIVE_CONTROL_SENSOR_COUNT];
	float trackingKp;
	float trackingKi;
	float trackingKd;
	float encoderKp;
	float encoderKi;
	int16_t trackingLimit;
	int16_t encoderLimit;
	int32_t encoderFullScaleCps;
	int32_t previousLeftFrontCount;
	int32_t previousLeftRearCount;
	int32_t previousRightFrontCount;
	int32_t previousRightRearCount;
	uint8_t encoderSynchronized;
} DriveControl_State;

static DriveControl_State g_drive;

static int16_t DriveControl_ClampPwm(int32_t value)
{
	if (value > DRIVE_CONTROL_PWM_MAX)
	{
		return DRIVE_CONTROL_PWM_MAX;
	}
	if (value < -DRIVE_CONTROL_PWM_MAX)
	{
		return -DRIVE_CONTROL_PWM_MAX;
	}
	return (int16_t)value;
}

static int16_t DriveControl_RoundFloat(float value)
{
	return (value >= 0.0f) ? (int16_t)(value + 0.5f) : (int16_t)(value - 0.5f);
}

static int32_t DriveControl_CommandToCps(int16_t pwm)
{
	int64_t scaled = (int64_t)pwm * g_drive.encoderFullScaleCps;
	if (scaled >= 0)
	{
		scaled += DRIVE_CONTROL_PWM_MAX / 2;
	}
	else
	{
		scaled -= DRIVE_CONTROL_PWM_MAX / 2;
	}
	return (int32_t)(scaled / DRIVE_CONTROL_PWM_MAX);
}

static uint8_t DriveControl_WeightsAreValid(const int16_t weights[DRIVE_CONTROL_SENSOR_COUNT])
{
	uint8_t index;
	if (weights == 0)
	{
		return 0U;
	}
	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		if ((weights[index] < -10000) || (weights[index] > 10000))
		{
			return 0U;
		}
		if ((index != 0U) && (weights[index] <= weights[index - 1U]))
		{
			return 0U;
		}
	}
	return 1U;
}

static int16_t DriveControl_CalculateTrackingError(uint8_t sensorBits, uint8_t *activeCount)
{
	int32_t sum = 0;
	uint8_t count = 0U;
	uint8_t index;

	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		if ((sensorBits & (uint8_t)(1U << index)) != 0U)
		{
			sum += g_drive.weights[index];
			count++;
		}
	}

	*activeCount = count;
	return (count == 0U) ? 0 : (int16_t)(sum / count);
}

static void DriveControl_ReadSensors(void)
{
#if DRIVE_CONTROL_ENABLE_HARDWARE
	g_drive.snapshot.sensorBits = Tracking_ReadAll();
#endif
}

static void DriveControl_WriteMotors(int16_t leftPwm, int16_t rightPwm)
{
#if DRIVE_CONTROL_ENABLE_HARDWARE
	Motor_SetSpeeds(leftPwm, rightPwm);
#else
	(void)leftPwm;
	(void)rightPwm;
#endif
}

static void DriveControl_UpdateTracking(float dtSeconds)
{
	uint8_t activeCount = 0U;
	float correction;

	g_drive.snapshot.trackingError =
		DriveControl_CalculateTrackingError(g_drive.snapshot.sensorBits, &activeCount);

	if (g_drive.snapshot.mode == DRIVE_MODE_STRAIGHT)
	{
		g_drive.snapshot.trackingCorrection = 0;
		g_drive.snapshot.desiredLeftPwm = g_drive.snapshot.speed;
		g_drive.snapshot.desiredRightPwm = g_drive.snapshot.speed;
		g_drive.snapshot.trackingState = (activeCount == 0U) ? 4U : 0U;
		return;
	}

	if (activeCount == 0U)
	{
		SimplePID_Reset(&g_drive.trackingPid);
		g_drive.snapshot.trackingCorrection = 0;
		g_drive.snapshot.desiredLeftPwm = g_drive.snapshot.speed;
		g_drive.snapshot.desiredRightPwm = g_drive.snapshot.speed;
		g_drive.snapshot.trackingState = 4U;
		return;
	}

	correction = SimplePID_Update(&g_drive.trackingPid,
								  (float)g_drive.snapshot.trackingError,
								  dtSeconds);
	g_drive.snapshot.trackingCorrection = DriveControl_RoundFloat(correction);
	g_drive.snapshot.desiredLeftPwm = DriveControl_ClampPwm(
		(int32_t)g_drive.snapshot.speed + g_drive.snapshot.trackingCorrection);
	g_drive.snapshot.desiredRightPwm = DriveControl_ClampPwm(
		(int32_t)g_drive.snapshot.speed - g_drive.snapshot.trackingCorrection);
	g_drive.snapshot.trackingState = (activeCount >= 3U) ? 3U : 0U;
}

static void DriveControl_UpdateEncoderMeasurements(uint16_t elapsedMs)
{
	int32_t lf = Encoder_GetLeftFrontCount();
	int32_t lr = Encoder_GetLeftRearCount();
	int32_t rf = Encoder_GetRightFrontCount();
	int32_t rr = Encoder_GetRightRearCount();
	int32_t leftDelta;
	int32_t rightDelta;

	if ((g_drive.encoderSynchronized == 0U) || (elapsedMs == 0U))
	{
		g_drive.previousLeftFrontCount = lf;
		g_drive.previousLeftRearCount = lr;
		g_drive.previousRightFrontCount = rf;
		g_drive.previousRightRearCount = rr;
		g_drive.encoderSynchronized = 1U;
		g_drive.snapshot.leftMeasuredCps = 0;
		g_drive.snapshot.rightMeasuredCps = 0;
		return;
	}

	leftDelta = ((lf - g_drive.previousLeftFrontCount) +
				 (lr - g_drive.previousLeftRearCount)) / 2;
	rightDelta = ((rf - g_drive.previousRightFrontCount) +
				  (rr - g_drive.previousRightRearCount)) / 2;

	g_drive.previousLeftFrontCount = lf;
	g_drive.previousLeftRearCount = lr;
	g_drive.previousRightFrontCount = rf;
	g_drive.previousRightRearCount = rr;

	g_drive.snapshot.leftMeasuredCps =
		(int32_t)(((int64_t)leftDelta * 1000LL) / elapsedMs);
	g_drive.snapshot.rightMeasuredCps =
		(int32_t)(((int64_t)rightDelta * 1000LL) / elapsedMs);
}

static int16_t DriveControl_UpdateSpeedSide(SimplePID *pid,
											int16_t desiredPwm,
											int32_t targetCps,
											int32_t measuredCps,
											float dtSeconds)
{
	float correction;

	if (desiredPwm == 0)
	{
		SimplePID_Reset(pid);
		return 0;
	}

	correction = SimplePID_Update(pid, (float)(targetCps - measuredCps), dtSeconds);
	return DriveControl_ClampPwm((int32_t)desiredPwm + DriveControl_RoundFloat(correction));
}

static void DriveControl_UpdateSpeedPi(uint16_t elapsedMs, float dtSeconds)
{
	g_drive.snapshot.leftTargetCps =
		DriveControl_CommandToCps(g_drive.snapshot.desiredLeftPwm);
	g_drive.snapshot.rightTargetCps =
		DriveControl_CommandToCps(g_drive.snapshot.desiredRightPwm);

	DriveControl_UpdateEncoderMeasurements(elapsedMs);

	if (g_drive.snapshot.encoderClosed == 0U)
	{
		g_drive.snapshot.appliedLeftPwm = g_drive.snapshot.desiredLeftPwm;
		g_drive.snapshot.appliedRightPwm = g_drive.snapshot.desiredRightPwm;
		return;
	}

	g_drive.snapshot.appliedLeftPwm = DriveControl_UpdateSpeedSide(
		&g_drive.leftSpeedPid,
		g_drive.snapshot.desiredLeftPwm,
		g_drive.snapshot.leftTargetCps,
		g_drive.snapshot.leftMeasuredCps,
		dtSeconds);
	g_drive.snapshot.appliedRightPwm = DriveControl_UpdateSpeedSide(
		&g_drive.rightSpeedPid,
		g_drive.snapshot.desiredRightPwm,
		g_drive.snapshot.rightTargetCps,
		g_drive.snapshot.rightMeasuredCps,
		dtSeconds);
}

void DriveControl_LoadDefaults(void)
{
	static const int16_t defaultWeights[DRIVE_CONTROL_SENSOR_COUNT] = {
		-7000, -5000, -3000, -1000, 1000, 3000, 5000, 7000
	};
	uint8_t index;

	g_drive.snapshot.running = 0U;
	g_drive.snapshot.mode = DRIVE_MODE_TRACK;
	g_drive.snapshot.encoderClosed = 0U;
	g_drive.snapshot.speed = 400;
	g_drive.trackingKp = 0.140000f;
	g_drive.trackingKi = 0.000000f;
	g_drive.trackingKd = 0.000250f;
	g_drive.trackingLimit = 280;
	g_drive.encoderKp = 0.020000f;
	g_drive.encoderKi = 0.000000f;
	g_drive.encoderLimit = 100;
	g_drive.encoderFullScaleCps = 5000;

	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		g_drive.weights[index] = defaultWeights[index];
	}

	SimplePID_Init(&g_drive.trackingPid, g_drive.trackingKp,
				   g_drive.trackingKi, g_drive.trackingKd,
				   (float)g_drive.trackingLimit);
	SimplePID_Init(&g_drive.leftSpeedPid, g_drive.encoderKp,
				   g_drive.encoderKi, 0.0f, (float)g_drive.encoderLimit);
	SimplePID_Init(&g_drive.rightSpeedPid, g_drive.encoderKp,
				   g_drive.encoderKi, 0.0f, (float)g_drive.encoderLimit);
	DriveControl_Reset();
}

void DriveControl_Init(void)
{
#if DRIVE_CONTROL_ENABLE_HARDWARE
	Motor_Init();
	Tracking_Init();
#endif
	Encoder_Init();
	DriveControl_LoadDefaults();
}

void DriveControl_Reset(void)
{
	SimplePID_Reset(&g_drive.trackingPid);
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	g_drive.encoderSynchronized = 0U;
	g_drive.snapshot.trackingError = 0;
	g_drive.snapshot.trackingCorrection = 0;
	g_drive.snapshot.desiredLeftPwm = 0;
	g_drive.snapshot.desiredRightPwm = 0;
	g_drive.snapshot.appliedLeftPwm = 0;
	g_drive.snapshot.appliedRightPwm = 0;
	g_drive.snapshot.leftTargetCps = 0;
	g_drive.snapshot.rightTargetCps = 0;
	g_drive.snapshot.leftMeasuredCps = 0;
	g_drive.snapshot.rightMeasuredCps = 0;
	g_drive.snapshot.trackingState = 0U;
	DriveControl_WriteMotors(0, 0);
}

void DriveControl_Start(void)
{
	DriveControl_Reset();
	g_drive.snapshot.running = 1U;
}

void DriveControl_Stop(void)
{
	g_drive.snapshot.running = 0U;
	DriveControl_Reset();
}

uint8_t DriveControl_SetMode(DriveControl_Mode mode)
{
	if ((mode != DRIVE_MODE_TRACK) && (mode != DRIVE_MODE_STRAIGHT))
	{
		return 0U;
	}
	g_drive.snapshot.mode = mode;
	DriveControl_Reset();
	return 1U;
}

uint8_t DriveControl_SetSpeed(int16_t speed)
{
	if ((speed < 0) || (speed > DRIVE_CONTROL_PWM_MAX))
	{
		return 0U;
	}
	g_drive.snapshot.speed = speed;
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	return 1U;
}

uint8_t DriveControl_SetTrackingGains(float kp, float ki, float kd)
{
	if ((kp < 0.0f) || (kp > 2.0f) ||
		(ki < 0.0f) || (ki > 2.0f) ||
		(kd < 0.0f) || (kd > 1.0f))
	{
		return 0U;
	}
	g_drive.trackingKp = kp;
	g_drive.trackingKi = ki;
	g_drive.trackingKd = kd;
	SimplePID_SetGains(&g_drive.trackingPid, kp, ki, kd);
	return 1U;
}

uint8_t DriveControl_SetTrackingLimit(int16_t limit)
{
	if ((limit < 0) || (limit > 500))
	{
		return 0U;
	}
	g_drive.trackingLimit = limit;
	SimplePID_SetOutputLimit(&g_drive.trackingPid, (float)limit);
	return 1U;
}

uint8_t DriveControl_SetWeight(uint8_t channel, int16_t weight)
{
	int16_t candidate[DRIVE_CONTROL_SENSOR_COUNT];
	uint8_t index;

	if ((channel < 1U) || (channel > DRIVE_CONTROL_SENSOR_COUNT))
	{
		return 0U;
	}
	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		candidate[index] = g_drive.weights[index];
	}
	candidate[channel - 1U] = weight;
	return DriveControl_SetWeights(candidate);
}

uint8_t DriveControl_SetWeights(const int16_t weights[DRIVE_CONTROL_SENSOR_COUNT])
{
	uint8_t index;
	if (DriveControl_WeightsAreValid(weights) == 0U)
	{
		return 0U;
	}
	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		g_drive.weights[index] = weights[index];
	}
	SimplePID_Reset(&g_drive.trackingPid);
	return 1U;
}

void DriveControl_SetEncoderClosed(uint8_t enabled)
{
	g_drive.snapshot.encoderClosed = (enabled != 0U) ? 1U : 0U;
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	g_drive.encoderSynchronized = 0U;
}

uint8_t DriveControl_SetEncoderGains(float kp, float ki)
{
	if ((kp < 0.0f) || (kp > 1.0f) || (ki < 0.0f) || (ki > 10.0f))
	{
		return 0U;
	}
	g_drive.encoderKp = kp;
	g_drive.encoderKi = ki;
	SimplePID_SetGains(&g_drive.leftSpeedPid, kp, ki, 0.0f);
	SimplePID_SetGains(&g_drive.rightSpeedPid, kp, ki, 0.0f);
	return 1U;
}

uint8_t DriveControl_SetEncoderFullScaleCps(int32_t fullScaleCps)
{
	if ((fullScaleCps < 100) || (fullScaleCps > 50000))
	{
		return 0U;
	}
	g_drive.encoderFullScaleCps = fullScaleCps;
	return 1U;
}

uint8_t DriveControl_SetEncoderLimit(int16_t limit)
{
	if ((limit < 0) || (limit > DRIVE_CONTROL_PWM_MAX))
	{
		return 0U;
	}
	g_drive.encoderLimit = limit;
	SimplePID_SetOutputLimit(&g_drive.leftSpeedPid, (float)limit);
	SimplePID_SetOutputLimit(&g_drive.rightSpeedPid, (float)limit);
	return 1U;
}

void DriveControl_Update(uint16_t elapsedMs)
{
	float dtSeconds;

	if (elapsedMs == 0U)
	{
		elapsedMs = 1U;
	}
	dtSeconds = (float)elapsedMs / 1000.0f;

	DriveControl_ReadSensors();

	if (g_drive.snapshot.running == 0U)
	{
		g_drive.snapshot.desiredLeftPwm = 0;
		g_drive.snapshot.desiredRightPwm = 0;
		g_drive.snapshot.appliedLeftPwm = 0;
		g_drive.snapshot.appliedRightPwm = 0;
		DriveControl_UpdateEncoderMeasurements(elapsedMs);
		DriveControl_WriteMotors(0, 0);
		return;
	}

	DriveControl_UpdateTracking(dtSeconds);
	DriveControl_UpdateSpeedPi(elapsedMs, dtSeconds);
	DriveControl_WriteMotors(g_drive.snapshot.appliedLeftPwm,
							  g_drive.snapshot.appliedRightPwm);
}

void DriveControl_GetSnapshot(DriveControl_Snapshot *snapshot)
{
	if (snapshot == 0)
	{
		return;
	}
	*snapshot = g_drive.snapshot;
}

void DriveControl_FormatSensorBits(char output[9])
{
	uint8_t index;
	if (output == 0)
	{
		return;
	}
	for (index = 0U; index < DRIVE_CONTROL_SENSOR_COUNT; index++)
	{
		output[index] = ((g_drive.snapshot.sensorBits & (uint8_t)(1U << index)) != 0U) ? '1' : '0';
	}
	output[8] = '\0';
}

float DriveControl_GetTrackingKp(void) { return g_drive.trackingKp; }
float DriveControl_GetTrackingKi(void) { return g_drive.trackingKi; }
float DriveControl_GetTrackingKd(void) { return g_drive.trackingKd; }
int16_t DriveControl_GetTrackingLimit(void) { return g_drive.trackingLimit; }

int16_t DriveControl_GetWeight(uint8_t channel)
{
	if ((channel < 1U) || (channel > DRIVE_CONTROL_SENSOR_COUNT))
	{
		return 0;
	}
	return g_drive.weights[channel - 1U];
}

uint8_t DriveControl_GetEncoderClosed(void) { return g_drive.snapshot.encoderClosed; }
float DriveControl_GetEncoderKp(void) { return g_drive.encoderKp; }
float DriveControl_GetEncoderKi(void) { return g_drive.encoderKi; }
int32_t DriveControl_GetEncoderFullScaleCps(void) { return g_drive.encoderFullScaleCps; }
int16_t DriveControl_GetEncoderLimit(void) { return g_drive.encoderLimit; }
