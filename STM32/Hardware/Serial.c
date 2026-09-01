/*
 * 双串口硬件驱动。
 *
 * USART1/PA9-PA10 连接 HC-04/HC-05，保持 9600 8N1；USART2/PD5-PD6
 * 连接树莓派，使用 115200 8N1。两个端口各自拥有 RX 中断环形缓冲，TX
 * 由上层为每个端口配置的独立 ProtocolTx 任务串行发送。
 */
#include "Serial.h"
#include "BoardPins.h"
#include "BoardClock.h"
#include <stdio.h>

typedef struct
{
	USART_TypeDef *usart;
	GPIO_TypeDef *gpio;
	uint8_t txPin;
	uint8_t rxPin;
	uint8_t alternateFunction;
	uint32_t peripheralClockHz;
	uint32_t baud;
} Serial_PortConfig;

static const Serial_PortConfig g_portConfig[SERIAL_PORT_COUNT] =
{
	{
		BOARD_BT_USART, BOARD_BT_PORT, BOARD_BT_TX_PIN, BOARD_BT_RX_PIN,
		BOARD_BT_AF, BOARD_APB2_CLOCK_HZ, SERIAL_BT_BAUD
	},
	{
		BOARD_PI_USART, BOARD_PI_PORT, BOARD_PI_TX_PIN, BOARD_PI_RX_PIN,
		BOARD_PI_AF, BOARD_APB1_CLOCK_HZ, SERIAL_PI_BAUD
	}
};

static uint8_t g_rxBuffer[SERIAL_PORT_COUNT][SERIAL_RX_BUFFER_SIZE];
static volatile uint16_t g_rxHead[SERIAL_PORT_COUNT];
static volatile uint16_t g_rxTail[SERIAL_PORT_COUNT];

static uint8_t Serial_IsValidPort(Serial_Port port)
{
	return ((uint32_t)port < (uint32_t)SERIAL_PORT_COUNT) ? 1U : 0U;
}

static void Serial_SetAlternateFunction(GPIO_TypeDef *gpio, uint8_t pin,
										uint8_t alternateFunction)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	gpio->AFR[pin >> 3U] =
		(gpio->AFR[pin >> 3U] & ~(0xFUL << shift)) |
		((uint32_t)alternateFunction << shift);
}

static void Serial_ConfigureGpio(const Serial_PortConfig *config)
{
	uint32_t txShift = (uint32_t)config->txPin * 2U;
	uint32_t rxShift = (uint32_t)config->rxPin * 2U;
	uint32_t pinMask = (uint32_t)BOARD_PIN_MASK(config->txPin) |
		(uint32_t)BOARD_PIN_MASK(config->rxPin);

	config->gpio->MODER =
		(config->gpio->MODER & ~((3UL << txShift) | (3UL << rxShift))) |
		(2UL << txShift) | (2UL << rxShift);
	config->gpio->OTYPER &= ~pinMask;
	config->gpio->OSPEEDR =
		(config->gpio->OSPEEDR & ~((3UL << txShift) | (3UL << rxShift))) |
		(2UL << txShift) | (2UL << rxShift);
	config->gpio->PUPDR =
		(config->gpio->PUPDR & ~((3UL << txShift) | (3UL << rxShift))) |
		(1UL << rxShift);
	Serial_SetAlternateFunction(config->gpio, config->txPin,
								config->alternateFunction);
	Serial_SetAlternateFunction(config->gpio, config->rxPin,
								config->alternateFunction);
}

static void Serial_ConfigurePeripheral(Serial_Port port)
{
	const Serial_PortConfig *config = &g_portConfig[port];

	g_rxHead[port] = 0U;
	g_rxTail[port] = 0U;
	config->usart->CR1 = 0U;
	config->usart->CR2 = 0U;
	config->usart->CR3 = 0U;
	/* OVER8=0 时 BRR 可直接按 PCLK/baud 四舍五入编码。 */
	config->usart->BRR =
		(config->peripheralClockHz + (config->baud / 2UL)) / config->baud;
	config->usart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE |
		USART_CR1_UE;
}

static void Serial_HandleInterrupt(Serial_Port port)
{
	USART_TypeDef *usart = g_portConfig[port].usart;
	uint32_t status = usart->SR;
	uint32_t errorMask = USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE;

	if ((status & errorMask) != 0U)
	{
		/* F4 通过先读 SR、再读 DR 清错误；错误字节不进入命令流。 */
		(void)usart->DR;
		return;
	}
	if ((status & USART_SR_RXNE) != 0U)
	{
		uint16_t head = g_rxHead[port];
		uint16_t next;
		g_rxBuffer[port][head] = (uint8_t)usart->DR;
		next = (uint16_t)((head + 1U) % SERIAL_RX_BUFFER_SIZE);
		g_rxHead[port] = next;
		if (next == g_rxTail[port])
		{
			/* 缓冲满时丢最旧字节，保留最新命令流，且 ISR 永不阻塞。 */
			g_rxTail[port] =
				(uint16_t)((g_rxTail[port] + 1U) % SERIAL_RX_BUFFER_SIZE);
		}
	}
}

int fputc(int character, FILE *file)
{
	(void)file;
	/* 禁止 printf 绕过 ProtocolTx，避免多任务输出交错或发错端口。 */
	return character;
}

void USART1_IRQHandler(void)
{
	Serial_HandleInterrupt(SERIAL_PORT_BLUETOOTH);
}

void USART2_IRQHandler(void)
{
	Serial_HandleInterrupt(SERIAL_PORT_RASPBERRY);
}

void Serial_Init(void)
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN;
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	(void)RCC->AHB1ENR;
	(void)RCC->APB1ENR;
	(void)RCC->APB2ENR;

	Serial_ConfigureGpio(&g_portConfig[SERIAL_PORT_BLUETOOTH]);
	Serial_ConfigureGpio(&g_portConfig[SERIAL_PORT_RASPBERRY]);
	Serial_ConfigurePeripheral(SERIAL_PORT_BLUETOOTH);
	Serial_ConfigurePeripheral(SERIAL_PORT_RASPBERRY);

	NVIC_SetPriority(USART1_IRQn, 2U);
	NVIC_SetPriority(USART2_IRQn, 2U);
	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_EnableIRQ(USART2_IRQn);
}

void Serial_SendByte(Serial_Port port, uint8_t byte)
{
	USART_TypeDef *usart;
	if (Serial_IsValidPort(port) == 0U)
	{
		return;
	}
	usart = g_portConfig[port].usart;
	while ((usart->SR & USART_SR_TXE) == 0U)
	{
	}
	usart->DR = byte;
}

void Serial_SendArray(Serial_Port port, const uint8_t *array, uint16_t length)
{
	uint16_t index;
	if ((Serial_IsValidPort(port) == 0U) || (array == 0))
	{
		return;
	}
	for (index = 0U; index < length; index++)
	{
		Serial_SendByte(port, array[index]);
	}
}

void Serial_SendString(Serial_Port port, const char *string)
{
	if ((Serial_IsValidPort(port) == 0U) || (string == 0))
	{
		return;
	}
	while (*string != '\0')
	{
		Serial_SendByte(port, (uint8_t)*string++);
	}
}

void Serial_SendNumber(Serial_Port port, uint32_t number, uint8_t length)
{
	uint8_t index;
	uint32_t divisor = 1U;
	if ((Serial_IsValidPort(port) == 0U) || (length == 0U))
	{
		return;
	}
	for (index = 1U; index < length; index++)
	{
		divisor *= 10U;
	}
	for (index = 0U; index < length; index++)
	{
		Serial_SendByte(port,
			(uint8_t)(((number / divisor) % 10U) + (uint32_t)'0'));
		divisor /= 10U;
	}
}

uint16_t Serial_Available(Serial_Port port)
{
	uint16_t head;
	uint16_t tail;
	if (Serial_IsValidPort(port) == 0U)
	{
		return 0U;
	}
	head = g_rxHead[port];
	tail = g_rxTail[port];
	if (head >= tail)
	{
		return (uint16_t)(head - tail);
	}
	return (uint16_t)(SERIAL_RX_BUFFER_SIZE + head - tail);
}

uint8_t Serial_ReadByte(Serial_Port port)
{
	uint8_t value = 0U;
	if (Serial_IsValidPort(port) == 0U)
	{
		return 0U;
	}
	if (g_rxHead[port] != g_rxTail[port])
	{
		value = g_rxBuffer[port][g_rxTail[port]];
		g_rxTail[port] =
			(uint16_t)((g_rxTail[port] + 1U) % SERIAL_RX_BUFFER_SIZE);
	}
	return value;
}
