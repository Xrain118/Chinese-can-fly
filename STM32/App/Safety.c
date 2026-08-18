#include "Safety.h"
#include "BoardPins.h"
#include "PowerMonitor.h"
#include "DriveControl.h"

typedef struct
{
	uint32_t faultFlags;
	uint32_t lastCommandMs;
	uint32_t lastImuSampleMs;
	uint32_t lastBatterySampleMs;
	uint32_t lowBatterySinceMs;
	uint32_t encoderFaultSinceMs[4];
	uint8_t encoderFaultTiming[4];
	uint8_t lowBatteryTiming;
	uint8_t imuHealthy;
	uint8_t emergencyStopActive;
	uint8_t batteryAdcHealthy;
	uint32_t currentTimeMs;
} Safety_State;

/* Safety 只维护故障判定和停机请求；真正的 PWM 清零仍走 DriveControl_Stop。 */
static Safety_State g_safety;

static int32_t Safety_Abs32(int32_t value)
{
	return (value < 0) ? -value : value;
}

static uint8_t Safety_ReadEmergencyStop(void)
{
	uint8_t level =
		((BOARD_ESTOP_PORT->IDR & BOARD_PIN_MASK(BOARD_ESTOP_PIN)) != 0U) ? 1U : 0U;
	return (level == BOARD_ESTOP_ACTIVE_LEVEL) ? 1U : 0U;
}

static void Safety_CheckEncoder(uint8_t index, int16_t command, int32_t measured,
								uint32_t nowMs)
{
	/* 有足够 PWM 却长时间没有 CPS，认为编码器/接线/电机链路异常。 */
	if ((Safety_Abs32(command) >= SAFETY_ENCODER_TEST_PWM) &&
		(Safety_Abs32(measured) < SAFETY_ENCODER_MIN_CPS))
	{
		if (g_safety.encoderFaultTiming[index] == 0U)
		{
			g_safety.encoderFaultTiming[index] = 1U;
			g_safety.encoderFaultSinceMs[index] = nowMs;
		}
		else if ((uint32_t)(nowMs - g_safety.encoderFaultSinceMs[index]) >=
				 SAFETY_ENCODER_DELAY_MS)
		{
			g_safety.faultFlags |= SAFETY_FAULT_ENCODER;
		}
	}
	else
	{
		g_safety.encoderFaultTiming[index] = 0U;
	}
}

static void Safety_UpdateInputs(uint32_t nowMs)
{
	uint32_t batteryMv;
	g_safety.currentTimeMs = nowMs;

	g_safety.emergencyStopActive = Safety_ReadEmergencyStop();
	if (g_safety.emergencyStopActive != 0U)
	{
		g_safety.faultFlags |= SAFETY_FAULT_ESTOP;
	}

	if ((uint32_t)(nowMs - g_safety.lastBatterySampleMs) >= 20U)
	{
		/* 电池 ADC 不必每个控制周期都采，20ms 足够支撑低压保护。 */
		g_safety.lastBatterySampleMs = nowMs;
		if (PowerMonitor_Update() == 0U)
		{
			g_safety.batteryAdcHealthy = 0U;
			g_safety.faultFlags |= SAFETY_FAULT_BATTERY_ADC;
		}
		else
		{
			g_safety.batteryAdcHealthy = 1U;
		}
	}
	batteryMv = PowerMonitor_GetBatteryMv();
	if ((batteryMv != 0U) && (batteryMv < POWER_BATTERY_LOW_MV))
	{
		/* 低压使用延迟确认，避免电机启动瞬间压降误触发。 */
		if (g_safety.lowBatteryTiming == 0U)
		{
			g_safety.lowBatteryTiming = 1U;
			g_safety.lowBatterySinceMs = nowMs;
		}
		else if ((uint32_t)(nowMs - g_safety.lowBatterySinceMs) >=
				 SAFETY_LOW_BATTERY_DELAY_MS)
		{
			g_safety.faultFlags |= SAFETY_FAULT_LOW_BATTERY;
		}
	}
	else
	{
		g_safety.lowBatteryTiming = 0U;
	}

	if ((g_safety.imuHealthy == 0U) ||
		((uint32_t)(nowMs - g_safety.lastImuSampleMs) > SAFETY_IMU_TIMEOUT_MS))
	{
		g_safety.faultFlags |= SAFETY_FAULT_IMU;
	}
}

void Safety_Init(uint32_t nowMs, uint8_t imuReady)
{
	uint8_t index;
	uint32_t shift = BOARD_ESTOP_PIN * 2U;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
	(void)RCC->AHB1ENR;
	BOARD_ESTOP_PORT->MODER &= ~(3UL << shift);
	BOARD_ESTOP_PORT->PUPDR =
		(BOARD_ESTOP_PORT->PUPDR & ~(3UL << shift)) | (1UL << shift);

	PowerMonitor_Init();
	g_safety.faultFlags = 0U;
	g_safety.lastCommandMs = nowMs;
	g_safety.lastImuSampleMs = nowMs;
	g_safety.lastBatterySampleMs = nowMs - 20U;
	g_safety.lowBatterySinceMs = nowMs;
	g_safety.lowBatteryTiming = 0U;
	g_safety.imuHealthy = (imuReady != 0U) ? 1U : 0U;
	g_safety.emergencyStopActive = 0U;
	g_safety.batteryAdcHealthy = 0U;
	g_safety.currentTimeMs = nowMs;
	for (index = 0U; index < 4U; index++)
	{
		g_safety.encoderFaultSinceMs[index] = nowMs;
		g_safety.encoderFaultTiming[index] = 0U;
	}
	Safety_UpdateInputs(nowMs);
}

void Safety_NotifyImuSample(uint32_t nowMs)
{
	g_safety.lastImuSampleMs = nowMs;
	g_safety.imuHealthy = 1U;
}

void Safety_KickCommunication(uint32_t nowMs)
{
	g_safety.lastCommandMs = nowMs;
}

void Safety_Update(uint32_t nowMs)
{
	DriveControl_Snapshot drive;

	Safety_UpdateInputs(nowMs);
	DriveControl_GetSnapshot(&drive);
	if ((drive.running != 0U) &&
		((uint32_t)(nowMs - g_safety.lastCommandMs) > SAFETY_COMM_TIMEOUT_MS))
	{
		/* Web/ROS 必须持续 PING 或 PWM/SPEED，运行中断联会触发停机。 */
		g_safety.faultFlags |= SAFETY_FAULT_COMM_TIMEOUT;
	}

	if ((drive.running != 0U) && (drive.encoderClosed != 0U))
	{
		Safety_CheckEncoder(0U, drive.desiredLeftPwm, drive.leftFrontCps, nowMs);
		Safety_CheckEncoder(1U, drive.desiredLeftPwm, drive.leftRearCps, nowMs);
		Safety_CheckEncoder(2U, drive.desiredRightPwm, drive.rightFrontCps, nowMs);
		Safety_CheckEncoder(3U, drive.desiredRightPwm, drive.rightRearCps, nowMs);
	}
	else
	{
		g_safety.encoderFaultTiming[0] = 0U;
		g_safety.encoderFaultTiming[1] = 0U;
		g_safety.encoderFaultTiming[2] = 0U;
		g_safety.encoderFaultTiming[3] = 0U;
	}

	if ((g_safety.faultFlags != 0U) && (drive.running != 0U))
	{
		/* 任一故障置位后立即停机，故障清除只允许在外部条件恢复后执行。 */
		DriveControl_Stop();
	}
}

uint8_t Safety_RequestStart(uint32_t nowMs)
{
	uint32_t batteryMv;

	g_safety.faultFlags &= ~SAFETY_FAULT_COMM_TIMEOUT;
	Safety_KickCommunication(nowMs);
	Safety_UpdateInputs(nowMs);
	batteryMv = PowerMonitor_GetBatteryMv();
	if ((g_safety.emergencyStopActive != 0U) ||
		(g_safety.batteryAdcHealthy == 0U) ||
		(batteryMv < POWER_BATTERY_LOW_MV) ||
		(g_safety.imuHealthy == 0U) ||
		((uint32_t)(nowMs - g_safety.lastImuSampleMs) > SAFETY_IMU_TIMEOUT_MS))
	{
		return 0U;
	}
	return (g_safety.faultFlags == 0U) ? 1U : 0U;
}

uint8_t Safety_ClearFaults(uint32_t nowMs)
{
	uint32_t minimumBatteryMv;

	Safety_UpdateInputs(nowMs);
	minimumBatteryMv =
		((g_safety.faultFlags & SAFETY_FAULT_LOW_BATTERY) != 0U) ?
		POWER_BATTERY_RECOVER_MV : POWER_BATTERY_LOW_MV;
	if ((g_safety.emergencyStopActive != 0U) ||
		(PowerMonitor_GetBatteryMv() < minimumBatteryMv) ||
		(g_safety.batteryAdcHealthy == 0U) ||
		(g_safety.imuHealthy == 0U) ||
		((uint32_t)(nowMs - g_safety.lastImuSampleMs) > SAFETY_IMU_TIMEOUT_MS))
	{
		return 0U;
	}
	g_safety.faultFlags = 0U;
	g_safety.lowBatteryTiming = 0U;
	Safety_KickCommunication(nowMs);
	return 1U;
}

uint32_t Safety_GetFaultFlags(void)
{
	return g_safety.faultFlags;
}

void Safety_GetSnapshot(Safety_Snapshot *snapshot)
{
	if (snapshot == 0)
	{
		return;
	}
	snapshot->faultFlags = g_safety.faultFlags;
	snapshot->batteryMv = PowerMonitor_GetBatteryMv();
	snapshot->watchdogAgeMs =
		(uint32_t)(g_safety.currentTimeMs - g_safety.lastCommandMs);
	snapshot->emergencyStopActive = g_safety.emergencyStopActive;
	snapshot->imuHealthy = g_safety.imuHealthy;
}
