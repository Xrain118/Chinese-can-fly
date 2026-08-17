#include "stm32f4xx.h"
#include "BoardClock.h"
#include "SystemTick.h"
#include "Serial.h"
#include "ICM42688.h"
#include "DriveControl.h"
#include "DebugProtocol.h"

int main(void)
{
	uint32_t lastControl;
	uint32_t lastTelemetry;
	uint8_t imuReady;
	ICM42688_RawSample imuSample;

	(void)BoardClock_Init();
	SystemTick_Init();
	DriveControl_Init();
	Serial_Init();
	imuReady = ICM42688_Init();
	DebugProtocol_Init();
	DebugProtocol_SendState();
	if (imuReady != 0U)
	{
		Serial_Printf("OK C=IMU,WHO=%u\r\n", ICM42688_ReadWhoAmI());
	}
	else
	{
		Serial_SendString("ERR C=IMU,M=WHO_AM_I\r\n");
	}

	lastControl = SystemTick_Millis();
	lastTelemetry = lastControl;
	while (1)
	{
		uint32_t now;
		uint32_t elapsed;

		DebugProtocol_Run();
		now = SystemTick_Millis();
		elapsed = now - lastControl;
		if (elapsed >= 10U)
		{
			if (elapsed > 100U) elapsed = 100U;
			lastControl = now;
			DriveControl_Update((uint16_t)elapsed);
		}
		if ((uint32_t)(now - lastTelemetry) >= 50U)
		{
			lastTelemetry = now;
			DebugProtocol_SendTelemetry();
		}
		if ((imuReady != 0U) && (ICM42688_DataReady() != 0U))
		{
			(void)ICM42688_ReadSample(&imuSample);
		}
	}
}
