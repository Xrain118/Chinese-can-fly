/*
 * 车端运动控制核心。
 *
 * 上层只提交“想要的左右 PWM / 模式 / 编码器参数”，本文件负责把这些需求
 * 合成为最终写给电机驱动的 PWM。状态只由 ControlTask 串行访问，其他任务
 * 通过快照读取，避免在 FreeRTOS 多任务下出现半更新的车体状态。
 */
#include "DriveControl.h"
#include "SimplePID.h"
#include "Encoder.h"

#if DRIVE_CONTROL_ENABLE_HARDWARE
#include "Motor.h"
#endif

typedef struct
{
	SimplePID leftSpeedPid;
	SimplePID rightSpeedPid;
	DriveControl_Snapshot snapshot;
	float encoderKp;
	float encoderKi;
	float encoderSyncKp;
	int16_t encoderLimit;
	int16_t encoderSyncLimit;
	int32_t encoderFullScaleCps;
	int32_t encoderSyncToleranceCps;
	int32_t previousLeftFrontCount;
	int32_t previousLeftRearCount;
	int32_t previousRightFrontCount;
	int32_t previousRightRearCount;
	uint8_t encoderSynchronized;
} DriveControl_State;

/* 驱动控制状态由 ControlTask 串行访问；遥测通过快照读取当前值。 */
static DriveControl_State g_drive;

static uint8_t DriveControl_CommandChangesDirection(int16_t oldCommand,
													 int16_t newCommand)
{
	/* 方向变化时清 PI，避免旧积分把刚反向的电机继续往原方向推。 */
	if ((oldCommand == 0) || (newCommand == 0))
	{
		return (oldCommand != newCommand) ? 1U : 0U;
	}
	return ((oldCommand > 0) != (newCommand > 0)) ? 1U : 0U;
}

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
	/* 满量程 CPS 是现场标定值，用它把 PWM 需求换算为闭环目标速度。 */
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

static void DriveControl_WriteMotors(int16_t leftPwm, int16_t rightPwm)
{
#if DRIVE_CONTROL_ENABLE_HARDWARE
	Motor_SetSpeeds(leftPwm, rightPwm);
#else
	(void)leftPwm;
	(void)rightPwm;
#endif
}

static void DriveControl_UpdateEncoderMeasurements(uint16_t elapsedMs)
{
	int32_t lf = Encoder_GetLeftFrontCount();
	int32_t lr = Encoder_GetLeftRearCount();
	int32_t rf = Encoder_GetRightFrontCount();
	int32_t rr = Encoder_GetRightRearCount();
	int32_t leftFrontDelta;
	int32_t leftRearDelta;
	int32_t rightFrontDelta;
	int32_t rightRearDelta;

	if ((g_drive.encoderSynchronized == 0U) || (elapsedMs == 0U))
	{
		/* 第一次采样只建立基准计数，下一周期才计算 CPS。 */
		g_drive.previousLeftFrontCount = lf;
		g_drive.previousLeftRearCount = lr;
		g_drive.previousRightFrontCount = rf;
		g_drive.previousRightRearCount = rr;
		g_drive.encoderSynchronized = 1U;
		g_drive.snapshot.leftFrontCps = 0;
		g_drive.snapshot.leftRearCps = 0;
		g_drive.snapshot.rightFrontCps = 0;
		g_drive.snapshot.rightRearCps = 0;
		g_drive.snapshot.leftMeasuredCps = 0;
		g_drive.snapshot.rightMeasuredCps = 0;
		return;
	}

	/* 编码器计数器持续累加，这里只关心本控制周期的增量并换算成每秒计数 CPS。 */
	leftFrontDelta = lf - g_drive.previousLeftFrontCount;
	leftRearDelta = lr - g_drive.previousLeftRearCount;
	rightFrontDelta = rf - g_drive.previousRightFrontCount;
	rightRearDelta = rr - g_drive.previousRightRearCount;

	g_drive.previousLeftFrontCount = lf;
	g_drive.previousLeftRearCount = lr;
	g_drive.previousRightFrontCount = rf;
	g_drive.previousRightRearCount = rr;

	g_drive.snapshot.leftFrontCps =
		(int32_t)(((int64_t)leftFrontDelta * 1000LL) / elapsedMs);
	g_drive.snapshot.leftRearCps =
		(int32_t)(((int64_t)leftRearDelta * 1000LL) / elapsedMs);
	g_drive.snapshot.rightFrontCps =
		(int32_t)(((int64_t)rightFrontDelta * 1000LL) / elapsedMs);
	g_drive.snapshot.rightRearCps =
		(int32_t)(((int64_t)rightRearDelta * 1000LL) / elapsedMs);
	g_drive.snapshot.leftMeasuredCps = (int32_t)(
		((int64_t)g_drive.snapshot.leftFrontCps + g_drive.snapshot.leftRearCps) / 2LL);
	g_drive.snapshot.rightMeasuredCps = (int32_t)(
		((int64_t)g_drive.snapshot.rightFrontCps + g_drive.snapshot.rightRearCps) / 2LL);
}

static int16_t DriveControl_UpdateSpeedCorrection(SimplePID *pid,
												  int16_t desiredPwm,
												  int32_t targetCps,
												  int32_t measuredCps,
												  float dtSeconds)
{
	if (desiredPwm == 0)
	{
		/* 目标为 0 时清积分，防止停车后再次启动带着上一次误差冲出去。 */
		SimplePID_Reset(pid);
		return 0;
	}

	return DriveControl_RoundFloat(
		SimplePID_Update(pid, (float)(targetCps - measuredCps), dtSeconds));
}

static int16_t DriveControl_UpdateEncoderSync(void)
{
	int32_t effectiveError;
	float correction;

	/* 同步 P 只修正左右差速误差，不改变整体前进/后退需求。 */
	g_drive.snapshot.encoderSyncError =
		(g_drive.snapshot.leftTargetCps - g_drive.snapshot.rightTargetCps) -
		(g_drive.snapshot.leftMeasuredCps - g_drive.snapshot.rightMeasuredCps);
	g_drive.snapshot.encoderSyncActive = 0U;
	g_drive.snapshot.encoderSyncCorrection = 0;

	if ((g_drive.snapshot.encoderSyncEnabled == 0U) ||
		(g_drive.encoderSyncKp <= 0.0f) || (g_drive.encoderSyncLimit <= 0) ||
		(g_drive.snapshot.leftTargetCps == 0) ||
		(g_drive.snapshot.rightTargetCps == 0) ||
		(((g_drive.snapshot.leftTargetCps > 0) !=
		  (g_drive.snapshot.rightTargetCps > 0))))
	{
		return 0;
	}

	g_drive.snapshot.encoderSyncActive = 1U;
	effectiveError = g_drive.snapshot.encoderSyncError;
	if (effectiveError > g_drive.encoderSyncToleranceCps)
	{
		effectiveError -= g_drive.encoderSyncToleranceCps;
	}
	else if (effectiveError < -g_drive.encoderSyncToleranceCps)
	{
		effectiveError += g_drive.encoderSyncToleranceCps;
	}
	else
	{
		effectiveError = 0;
	}

	correction = g_drive.encoderSyncKp * (float)effectiveError;
	if (correction > (float)g_drive.encoderSyncLimit)
	{
		correction = (float)g_drive.encoderSyncLimit;
	}
	else if (correction < -(float)g_drive.encoderSyncLimit)
	{
		correction = -(float)g_drive.encoderSyncLimit;
	}
	g_drive.snapshot.encoderSyncCorrection = DriveControl_RoundFloat(correction);
	return g_drive.snapshot.encoderSyncCorrection;
}

static void DriveControl_UpdateSpeedPi(uint16_t elapsedMs, float dtSeconds)
{
	int16_t leftCorrection;
	int16_t rightCorrection;
	int16_t syncCorrection;

	/*
	 * 闭环目标不是直接来自上位机速度单位，而是先把当前 PWM 需求按满量程 CPS
	 * 映射成左右目标轮速。这样 DIRECT 和 STRAIGHT 两种模式共用同一套速度 PI。
	 */
	g_drive.snapshot.leftTargetCps =
		DriveControl_CommandToCps(g_drive.snapshot.desiredLeftPwm);
	g_drive.snapshot.rightTargetCps =
		DriveControl_CommandToCps(g_drive.snapshot.desiredRightPwm);

	DriveControl_UpdateEncoderMeasurements(elapsedMs);

	if (g_drive.snapshot.encoderClosed == 0U)
	{
		/* 开环时仍计算目标/实测差值供遥测诊断，但不参与 PWM 输出。 */
		g_drive.snapshot.encoderSyncError =
			(g_drive.snapshot.leftTargetCps - g_drive.snapshot.rightTargetCps) -
			(g_drive.snapshot.leftMeasuredCps - g_drive.snapshot.rightMeasuredCps);
		g_drive.snapshot.encoderSyncActive = 0U;
		g_drive.snapshot.encoderSyncCorrection = 0;
		g_drive.snapshot.appliedLeftPwm = g_drive.snapshot.desiredLeftPwm;
		g_drive.snapshot.appliedRightPwm = g_drive.snapshot.desiredRightPwm;
		return;
	}

	leftCorrection = DriveControl_UpdateSpeedCorrection(
		&g_drive.leftSpeedPid,
		g_drive.snapshot.desiredLeftPwm,
		g_drive.snapshot.leftTargetCps,
		g_drive.snapshot.leftMeasuredCps,
		dtSeconds);
	rightCorrection = DriveControl_UpdateSpeedCorrection(
		&g_drive.rightSpeedPid,
		g_drive.snapshot.desiredRightPwm,
		g_drive.snapshot.rightTargetCps,
		g_drive.snapshot.rightMeasuredCps,
		dtSeconds);
	syncCorrection = DriveControl_UpdateEncoderSync();

	g_drive.snapshot.appliedLeftPwm = DriveControl_ClampPwm(
		(int32_t)g_drive.snapshot.desiredLeftPwm + leftCorrection + syncCorrection);
	g_drive.snapshot.appliedRightPwm = DriveControl_ClampPwm(
		(int32_t)g_drive.snapshot.desiredRightPwm + rightCorrection - syncCorrection);
}

void DriveControl_LoadDefaults(void)
{
	/* 默认保持开环、停车和保守 PI 参数，避免刚刷机上电就进入闭环输出。 */
	g_drive.snapshot.running = 0U;
	g_drive.snapshot.mode = DRIVE_MODE_DIRECT;
	g_drive.snapshot.encoderClosed = 0U;
	g_drive.snapshot.encoderSyncEnabled = 0U;
	g_drive.snapshot.speed = 0;
	g_drive.encoderKp = 0.020000f;
	g_drive.encoderKi = 0.000000f;
	g_drive.encoderLimit = 100;
	g_drive.encoderFullScaleCps = 5000;
	g_drive.encoderSyncKp = 0.010000f;
	g_drive.encoderSyncToleranceCps = 50;
	g_drive.encoderSyncLimit = 50;

	SimplePID_Init(&g_drive.leftSpeedPid, g_drive.encoderKp,
				   g_drive.encoderKi, (float)g_drive.encoderLimit);
	SimplePID_Init(&g_drive.rightSpeedPid, g_drive.encoderKp,
				   g_drive.encoderKi, (float)g_drive.encoderLimit);
	DriveControl_Reset();
}

void DriveControl_Init(void)
{
#if DRIVE_CONTROL_ENABLE_HARDWARE
	Motor_Init();
#endif
	Encoder_Init();
	DriveControl_LoadDefaults();
}

void DriveControl_Reset(void)
{
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	g_drive.encoderSynchronized = 0U;
	g_drive.snapshot.desiredLeftPwm = 0;
	g_drive.snapshot.desiredRightPwm = 0;
	g_drive.snapshot.appliedLeftPwm = 0;
	g_drive.snapshot.appliedRightPwm = 0;
	g_drive.snapshot.leftTargetCps = 0;
	g_drive.snapshot.rightTargetCps = 0;
	g_drive.snapshot.leftFrontCps = 0;
	g_drive.snapshot.leftRearCps = 0;
	g_drive.snapshot.rightFrontCps = 0;
	g_drive.snapshot.rightRearCps = 0;
	g_drive.snapshot.leftMeasuredCps = 0;
	g_drive.snapshot.rightMeasuredCps = 0;
	g_drive.snapshot.encoderSyncError = 0;
	g_drive.snapshot.encoderSyncCorrection = 0;
	g_drive.snapshot.encoderSyncActive = 0U;
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
	/* Stop 必须立即清零输出，确保故障链调用时不会保留上一帧 PWM。 */
	DriveControl_Reset();
}

uint8_t DriveControl_SetMode(DriveControl_Mode mode)
{
	if ((mode != DRIVE_MODE_DIRECT) && (mode != DRIVE_MODE_STRAIGHT))
	{
		return 0U;
	}
	g_drive.snapshot.mode = mode;
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	return 1U;
}

uint8_t DriveControl_SetSpeed(int16_t speed)
{
	int16_t previousLeft;
	int16_t previousRight;

	if ((speed < -DRIVE_CONTROL_PWM_MAX) || (speed > DRIVE_CONTROL_PWM_MAX))
	{
		return 0U;
	}
	g_drive.snapshot.speed = speed;
	if (g_drive.snapshot.mode == DRIVE_MODE_STRAIGHT)
	{
		/* STRAIGHT 模式把 SPEED 同时分配给左右轮；DIRECT 模式仅保存基准速度值。 */
		previousLeft = g_drive.snapshot.desiredLeftPwm;
		previousRight = g_drive.snapshot.desiredRightPwm;
		g_drive.snapshot.desiredLeftPwm = speed;
		g_drive.snapshot.desiredRightPwm = speed;
		if (DriveControl_CommandChangesDirection(previousLeft, speed) != 0U)
		{
			SimplePID_Reset(&g_drive.leftSpeedPid);
		}
		if (DriveControl_CommandChangesDirection(previousRight, speed) != 0U)
		{
			SimplePID_Reset(&g_drive.rightSpeedPid);
		}
	}
	return 1U;
}

uint8_t DriveControl_SetWheelPwm(int16_t leftPwm, int16_t rightPwm)
{
	int16_t previousLeft = g_drive.snapshot.desiredLeftPwm;
	int16_t previousRight = g_drive.snapshot.desiredRightPwm;

	if ((leftPwm < -DRIVE_CONTROL_PWM_MAX) || (leftPwm > DRIVE_CONTROL_PWM_MAX) ||
		(rightPwm < -DRIVE_CONTROL_PWM_MAX) || (rightPwm > DRIVE_CONTROL_PWM_MAX))
	{
		return 0U;
	}
	/* PWM/MOVE 是显式左右轮命令，收到后立即切回 DIRECT，避免被 SPEED 自动覆盖。 */
	g_drive.snapshot.mode = DRIVE_MODE_DIRECT;
	g_drive.snapshot.desiredLeftPwm = leftPwm;
	g_drive.snapshot.desiredRightPwm = rightPwm;
	if (DriveControl_CommandChangesDirection(previousLeft, leftPwm) != 0U)
	{
		SimplePID_Reset(&g_drive.leftSpeedPid);
	}
	if (DriveControl_CommandChangesDirection(previousRight, rightPwm) != 0U)
	{
		SimplePID_Reset(&g_drive.rightSpeedPid);
	}
	return 1U;
}

void DriveControl_SetEncoderClosed(uint8_t enabled)
{
	g_drive.snapshot.encoderClosed = (enabled != 0U) ? 1U : 0U;
	/* 开关速度环时重新同步编码器基准，首帧不用于闭环修正。 */
	SimplePID_Reset(&g_drive.leftSpeedPid);
	SimplePID_Reset(&g_drive.rightSpeedPid);
	g_drive.encoderSynchronized = 0U;
	g_drive.snapshot.encoderSyncActive = 0U;
	g_drive.snapshot.encoderSyncCorrection = 0;
}

uint8_t DriveControl_SetEncoderGains(float kp, float ki)
{
	if ((kp < 0.0f) || (kp > 1.0f) || (ki < 0.0f) || (ki > 10.0f))
	{
		return 0U;
	}
	g_drive.encoderKp = kp;
	g_drive.encoderKi = ki;
	SimplePID_SetGains(&g_drive.leftSpeedPid, kp, ki);
	SimplePID_SetGains(&g_drive.rightSpeedPid, kp, ki);
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

void DriveControl_SetEncoderSyncEnabled(uint8_t enabled)
{
	g_drive.snapshot.encoderSyncEnabled = (enabled != 0U) ? 1U : 0U;
	g_drive.snapshot.encoderSyncActive = 0U;
	g_drive.snapshot.encoderSyncCorrection = 0;
}

uint8_t DriveControl_SetEncoderSync(float kp, int32_t toleranceCps, int16_t limit)
{
	if ((kp < 0.0f) || (kp > 1.0f) ||
		(toleranceCps < 0) || (toleranceCps > 50000) ||
		(limit < 0) || (limit > DRIVE_CONTROL_PWM_MAX))
	{
		return 0U;
	}
	g_drive.encoderSyncKp = kp;
	g_drive.encoderSyncToleranceCps = toleranceCps;
	g_drive.encoderSyncLimit = limit;
	g_drive.snapshot.encoderSyncActive = 0U;
	g_drive.snapshot.encoderSyncCorrection = 0;
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

	if (g_drive.snapshot.mode == DRIVE_MODE_STRAIGHT)
	{
		/* STRAIGHT 的左右目标每周期都由 speed 刷新，保证 SPEED 后发时立即生效。 */
		g_drive.snapshot.desiredLeftPwm = g_drive.snapshot.speed;
		g_drive.snapshot.desiredRightPwm = g_drive.snapshot.speed;
	}

	if (g_drive.snapshot.running == 0U)
	{
		g_drive.snapshot.appliedLeftPwm = 0;
		g_drive.snapshot.appliedRightPwm = 0;
		DriveControl_UpdateEncoderMeasurements(elapsedMs);
		DriveControl_WriteMotors(0, 0);
		return;
	}

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

uint8_t DriveControl_GetEncoderClosed(void) { return g_drive.snapshot.encoderClosed; }
float DriveControl_GetEncoderKp(void) { return g_drive.encoderKp; }
float DriveControl_GetEncoderKi(void) { return g_drive.encoderKi; }
int32_t DriveControl_GetEncoderFullScaleCps(void) { return g_drive.encoderFullScaleCps; }
int16_t DriveControl_GetEncoderLimit(void) { return g_drive.encoderLimit; }
uint8_t DriveControl_GetEncoderSyncEnabled(void) { return g_drive.snapshot.encoderSyncEnabled; }
float DriveControl_GetEncoderSyncKp(void) { return g_drive.encoderSyncKp; }
int32_t DriveControl_GetEncoderSyncToleranceCps(void) { return g_drive.encoderSyncToleranceCps; }
int16_t DriveControl_GetEncoderSyncLimit(void) { return g_drive.encoderSyncLimit; }
