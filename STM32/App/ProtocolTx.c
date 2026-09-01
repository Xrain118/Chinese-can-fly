/*
 * 双串口下行发送队列。
 *
 * 每个 USART 都有独立队列和发送任务，因此 9600 baud 的蓝牙输出不会阻塞
 * 115200 baud 的树莓派链路。调用方始终以完整协议行为单位入队。
 */
#include "ProtocolTx.h"
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

static StaticQueue_t g_txQueueControl[SERIAL_PORT_COUNT];
static uint8_t g_txQueueStorage[SERIAL_PORT_COUNT]
	[PROTOCOL_TX_QUEUE_LENGTH * sizeof(ProtocolTx_Line)];
static QueueHandle_t g_txQueue[SERIAL_PORT_COUNT];

static uint8_t ProtocolTx_IsValidPort(Serial_Port port)
{
	return ((uint32_t)port < (uint32_t)SERIAL_PORT_COUNT) ? 1U : 0U;
}

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
	Serial_Port port;
	for (port = SERIAL_PORT_BLUETOOTH; port < SERIAL_PORT_COUNT; port++)
	{
		g_txQueue[port] = xQueueCreateStatic(
			PROTOCOL_TX_QUEUE_LENGTH,
			sizeof(ProtocolTx_Line),
			g_txQueueStorage[port],
			&g_txQueueControl[port]);
		configASSERT(g_txQueue[port] != 0);
	}
}

uint8_t ProtocolTx_SendString(Serial_Port port, const char *text)
{
	ProtocolTx_Line line;
	TickType_t waitTicks = 0U;

	if ((ProtocolTx_IsValidPort(port) == 0U) || (text == 0) ||
		(g_txQueue[port] == 0))
	{
		return 0U;
	}
	ProtocolTx_CopyText(&line, text);
	if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
	{
		/* 各端口只短暂等待自己的队列，不让一个慢口长期拖住调用方。 */
		waitTicks = pdMS_TO_TICKS(20U);
	}
	return (xQueueSend(g_txQueue[port], &line, waitTicks) == pdPASS) ? 1U : 0U;
}

uint8_t ProtocolTx_Printf(Serial_Port port, const char *format, ...)
{
	ProtocolTx_Line line;
	va_list args;
	int written;

	if ((ProtocolTx_IsValidPort(port) == 0U) || (format == 0))
	{
		return 0U;
	}
	va_start(args, format);
	written = vsnprintf(line.text, sizeof(line.text), format, args);
	va_end(args);
	if ((written < 0) || ((uint32_t)written >= sizeof(line.text)))
	{
		return 0U;
	}
	return ProtocolTx_SendString(port, line.text);
}

uint8_t ProtocolTx_BroadcastString(const char *text)
{
	Serial_Port port;
	uint8_t result = 1U;

	for (port = SERIAL_PORT_BLUETOOTH; port < SERIAL_PORT_COUNT; port++)
	{
		if (ProtocolTx_SendString(port, text) == 0U)
		{
			result = 0U;
		}
	}
	return result;
}

void ProtocolTx_RunSerialTask(Serial_Port port)
{
	ProtocolTx_Line line;

	configASSERT(ProtocolTx_IsValidPort(port) != 0U);
	for (;;)
	{
		if (xQueueReceive(g_txQueue[port], &line, portMAX_DELAY) == pdPASS)
		{
			Serial_SendString(port, line.text);
		}
	}
}
