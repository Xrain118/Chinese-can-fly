#include "ProtocolTx.h"
#include "Serial.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>

#define PROTOCOL_TX_QUEUE_LENGTH (8U)

typedef struct
{
	char text[PROTOCOL_TX_LINE_SIZE];
} ProtocolTx_Line;

/* 静态队列，不依赖 heap；SerialTxTask 是唯一真正碰 USART TX 的任务。 */
static StaticQueue_t g_txQueueControl;
static uint8_t g_txQueueStorage[PROTOCOL_TX_QUEUE_LENGTH * sizeof(ProtocolTx_Line)];
static QueueHandle_t g_txQueue;

static void ProtocolTx_CopyText(ProtocolTx_Line *line, const char *text)
{
	uint16_t index = 0U;

	if (line == 0)
	{
		return;
	}
	if (text == 0)
	{
		line->text[0] = '\0';
		return;
	}
	while ((text[index] != '\0') && (index < (PROTOCOL_TX_LINE_SIZE - 1U)))
	{
		line->text[index] = text[index];
		index++;
	}
	line->text[index] = '\0';
}

void ProtocolTx_Init(void)
{
	g_txQueue = xQueueCreateStatic(
		PROTOCOL_TX_QUEUE_LENGTH,
		sizeof(ProtocolTx_Line),
		g_txQueueStorage,
		&g_txQueueControl);
	configASSERT(g_txQueue != 0);
}

uint8_t ProtocolTx_SendString(const char *text)
{
	ProtocolTx_Line line;
	TickType_t waitTicks = 0U;

	if ((text == 0) || (g_txQueue == 0))
	{
		return 0U;
	}
	ProtocolTx_CopyText(&line, text);
	if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
	{
		/* 运行期短暂等待，避免启动连发状态帧时队列瞬间满。 */
		waitTicks = pdMS_TO_TICKS(20U);
	}
	return (xQueueSend(g_txQueue, &line, waitTicks) == pdPASS) ? 1U : 0U;
}

uint8_t ProtocolTx_Printf(const char *format, ...)
{
	ProtocolTx_Line line;
	va_list args;
	int written;

	if ((format == 0) || (g_txQueue == 0))
	{
		return 0U;
	}
	va_start(args, format);
	written = vsnprintf(line.text, sizeof(line.text), format, args);
	va_end(args);
	if ((written < 0) || ((uint32_t)written >= sizeof(line.text)))
	{
		/* 不发送被截断的协议行，宁可丢帧也不污染下行解析。 */
		return 0U;
	}
	return ProtocolTx_SendString(line.text);
}

void ProtocolTx_RunSerialTask(void)
{
	ProtocolTx_Line line;

	for (;;)
	{
		if (xQueueReceive(g_txQueue, &line, portMAX_DELAY) == pdPASS)
		{
			Serial_SendString(line.text);
		}
	}
}
