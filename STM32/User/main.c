/*
 * 固件入口。
 *
 * main 只做一次性初始化和任务创建；车辆运行后的闭环控制、协议收发和遥测
 * 都在 FreeRTOS 任务中完成。读启动流程时按这里的调用顺序往下追即可。
 */
#include "stm32f4xx.h"
#include "BoardClock.h"
#include "SystemTick.h"
#include "Serial.h"
#include "DriveControl.h"
#include "DebugProtocol.h"
#include "Safety.h"
#include "UgvTasks.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
	/* 上电阶段只做硬件和应用对象初始化；周期逻辑全部交给 FreeRTOS 任务。 */
	(void)BoardClock_Init();
	SystemTick_Init();
	DriveControl_Init();
	Serial_Init();
	Safety_Init(SystemTick_Millis());
	DebugProtocol_Init();
	UgvTasks_Init();
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
