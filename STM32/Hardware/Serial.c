/*
 * USART2 蓝牙/树莓派串口。
 *
 * RX 使用中断环形缓冲，协议任务按行消费；TX 不在这里排队，必须通过
 * ProtocolTx/SerialTxTask 串行发送，避免多个任务同时 printf 打乱协议帧。
 */
#include "Serial.h"
#include "BoardPins.h"
#include "BoardClock.h"
#include <stdio.h>

static uint8_t g_rxBuffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint16_t g_rxHead;
static volatile uint16_t g_rxTail;

static void Serial_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

int fputc(int character, FILE *file)
{
	(void)file;
	/* 禁止 printf 绕过 ProtocolTx/SerialTxTask，避免多任务输出交错。 */
	return character;
}

void USART2_IRQHandler(void)
{
	uint32_t status = BOARD_BT_USART->SR;
	if ((status & USART_SR_RXNE) != 0U)
	{
		uint16_t next;
		g_rxBuffer[g_rxHead] = (uint8_t)BOARD_BT_USART->DR;
		next = (uint16_t)((g_rxHead + 1U) % SERIAL_RX_BUFFER_SIZE);
		g_rxHead = next;
		if (next == g_rxTail)
		{
			/* 缓冲满时丢最旧字节，保留最新命令流，避免 ISR 阻塞控制任务。 */
			g_rxTail = (uint16_t)((g_rxTail + 1U) % SERIAL_RX_BUFFER_SIZE);
		}
	}
	else if ((status & USART_SR_ORE) != 0U)
	{
		(void)BOARD_BT_USART->DR;
	}
}

void Serial_Init(void)
{
	uint32_t txShift = BOARD_BT_TX_PIN * 2U;
	uint32_t rxShift = BOARD_BT_RX_PIN * 2U;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	(void)RCC->APB1ENR;

	BOARD_BT_PORT->MODER =
		(BOARD_BT_PORT->MODER & ~((3UL << txShift) | (3UL << rxShift))) |
		(2UL << txShift) | (2UL << rxShift);
	BOARD_BT_PORT->OTYPER &= ~(BOARD_PIN_MASK(BOARD_BT_TX_PIN) |
								BOARD_PIN_MASK(BOARD_BT_RX_PIN));
	BOARD_BT_PORT->OSPEEDR |= (2UL << txShift) | (2UL << rxShift);
	BOARD_BT_PORT->PUPDR =
		(BOARD_BT_PORT->PUPDR & ~((3UL << txShift) | (3UL << rxShift))) |
		(1UL << rxShift);
	Serial_SetAlternateFunction(BOARD_BT_PORT, BOARD_BT_TX_PIN, BOARD_BT_AF);
	Serial_SetAlternateFunction(BOARD_BT_PORT, BOARD_BT_RX_PIN, BOARD_BT_AF);

	g_rxHead = 0U;
	g_rxTail = 0U;
	BOARD_BT_USART->CR1 = 0U;
	BOARD_BT_USART->CR2 = 0U;
	BOARD_BT_USART->CR3 = 0U;
	/* USART2 挂在 APB1，BRR 直接按板级时钟和目标波特率四舍五入计算。 */
	BOARD_BT_USART->BRR = (BOARD_APB1_CLOCK_HZ + (SERIAL_BAUD / 2UL)) / SERIAL_BAUD;
	BOARD_BT_USART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE |
						 USART_CR1_UE;

	NVIC_SetPriority(USART2_IRQn, 2U);
	NVIC_EnableIRQ(USART2_IRQn);
}

void Serial_SendByte(uint8_t byte)
{
	while ((BOARD_BT_USART->SR & USART_SR_TXE) == 0U)
	{
	}
	BOARD_BT_USART->DR = byte;
}

void Serial_SendArray(const uint8_t *array, uint16_t length)
{
	uint16_t index;
	if (array == 0) return;
	for (index = 0U; index < length; index++)
	{
		Serial_SendByte(array[index]);
	}
}

void Serial_SendString(const char *string)
{
	if (string == 0) return;
	while (*string != '\0')
	{
		Serial_SendByte((uint8_t)*string++);
	}
}

void Serial_SendNumber(uint32_t number, uint8_t length)
{
	uint8_t index;
	uint32_t divisor = 1U;
	if (length == 0U) return;
	for (index = 1U; index < length; index++) divisor *= 10U;
	for (index = 0U; index < length; index++)
	{
		Serial_SendByte((uint8_t)(((number / divisor) % 10U) + (uint32_t)'0'));
		divisor /= 10U;
	}
}

uint16_t Serial_Available(void)
{
	uint16_t head = g_rxHead;
	uint16_t tail = g_rxTail;
	if (head >= tail) return (uint16_t)(head - tail);
	return (uint16_t)(SERIAL_RX_BUFFER_SIZE + head - tail);
}

uint8_t Serial_ReadByte(void)
{
	uint8_t value = 0U;
	if (g_rxHead != g_rxTail)
	{
		value = g_rxBuffer[g_rxTail];
		g_rxTail = (uint16_t)((g_rxTail + 1U) % SERIAL_RX_BUFFER_SIZE);
	}
	return value;
}
