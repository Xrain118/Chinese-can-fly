#include "UgvCommandQueue.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define UGV_COMMAND_QUEUE_LENGTH (12U)

/* 命令队列是协议层到控制层的唯一写入通道。 */
static StaticQueue_t g_commandQueueControl;
static uint8_t g_commandQueueStorage[UGV_COMMAND_QUEUE_LENGTH * sizeof(UgvCommand)];
static QueueHandle_t g_commandQueue;

void UgvCommandQueue_Init(void)
{
	g_commandQueue = xQueueCreateStatic(
		UGV_COMMAND_QUEUE_LENGTH,
		sizeof(UgvCommand),
		g_commandQueueStorage,
		&g_commandQueueControl);
	configASSERT(g_commandQueue != 0);
}

uint8_t UgvCommandQueue_Send(const UgvCommand *command)
{
	TickType_t waitTicks = 0U;

	if ((command == 0) || (g_commandQueue == 0))
	{
		return 0U;
	}
	if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
	{
		waitTicks = pdMS_TO_TICKS(5U);
	}
	return (xQueueSend(g_commandQueue, command, waitTicks) == pdPASS) ? 1U : 0U;
}

uint8_t UgvCommandQueue_Receive(UgvCommand *command, uint32_t waitMs)
{
	TickType_t waitTicks;

	if ((command == 0) || (g_commandQueue == 0))
	{
		return 0U;
	}
	waitTicks = (waitMs == 0U) ? 0U : pdMS_TO_TICKS(waitMs);
	return (xQueueReceive(g_commandQueue, command, waitTicks) == pdPASS) ? 1U : 0U;
}
