#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Serial.h"
#include "DriveControl.h"
#include "DebugProtocol.h"

int main(void)
{
	Serial_Init();
	DriveControl_Init();
	DebugProtocol_Init();
	DebugProtocol_SendState();

	while (1)
	{
		static uint16_t controlTick = 0U;
		static uint16_t telemetryTick = 0U;

		DebugProtocol_Run();
		Delay_ms(1);

		controlTick++;
		telemetryTick++;

		if (controlTick >= 10U)
		{
			DriveControl_Update(controlTick);
			controlTick = 0U;
		}
		if (telemetryTick >= 50U)
		{
			DebugProtocol_SendTelemetry();
			telemetryTick = 0U;
		}
	}
}
