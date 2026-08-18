/*
 * FreeRTOS 应用钩子。
 *
 * 本工程使用静态任务分配，IdleTask 的内存由这里提供。栈溢出、malloc 失败
 * 和 assert 都进入停机现场，避免控制系统在未知状态下继续驱动车辆。
 */
#include "FreeRTOS.h"
#include "task.h"

static StaticTask_t g_idleTaskControl;
static StackType_t g_idleTaskStack[configMINIMAL_STACK_SIZE];

/* configSUPPORT_STATIC_ALLOCATION=1 时，IdleTask 内存必须由应用层提供。 */
void vApplicationGetIdleTaskMemory(StaticTask_t **taskControl,
								   StackType_t **taskStack,
								   configSTACK_DEPTH_TYPE *taskStackSize)
{
	*taskControl = &g_idleTaskControl;
	*taskStack = g_idleTaskStack;
	*taskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationIdleHook(void)
{
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *taskName)
{
	(void)task;
	(void)taskName;
	/* 栈溢出后禁止继续驱动车辆，停在现场便于调试栈大小。 */
	taskDISABLE_INTERRUPTS();
	for (;;)
	{
	}
}

void vApplicationMallocFailedHook(void)
{
	/* 理论上工程不依赖 heap；如果仍触发，说明某处引入了动态分配。 */
	taskDISABLE_INTERRUPTS();
	for (;;)
	{
	}
}

void vAssertCalled(const char *file, uint32_t line)
{
	(void)file;
	(void)line;
	/* configASSERT 失败说明 RTOS 使用方式有问题，直接停机。 */
	taskDISABLE_INTERRUPTS();
	for (;;)
	{
	}
}
