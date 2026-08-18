#include "stm32f4xx.h"
#include "BoardClock.h"
#include "SystemTick.h"
#include "Serial.h"
#include "ICM42688.h"
#include "DriveControl.h"
#include "DebugProtocol.h"
#include "Safety.h"
#include "UgvTasks.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
	uint8_t imuReady;

	/* 上电阶段只做硬件和应用对象初始化；周期逻辑全部交给 FreeRTOS 任务。 */
	(void)BoardClock_Init();
	SystemTick_Init();
	DriveControl_Init();
	Serial_Init();
	imuReady = ICM42688_Init();
	Safety_Init(SystemTick_Millis(), imuReady);
	DebugProtocol_Init();
	UgvTasks_Init(imuReady);
	if (UgvTasks_Start() != 0U)
	{
		vTaskStartScheduler();
	}

	/* 调度器未启动通常意味着静态任务创建失败，立即停机并留在现场。 */
	DriveControl_Stop();
	for (;;)
	{
	}
}
