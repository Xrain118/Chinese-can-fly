#include "Serial.h"
#include <stdio.h>
#include <stdarg.h>

/** 接收环形缓冲及其读写指针（写=head，读=tail）。 */
static uint8_t s_rxBuffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint16_t s_rxHead = 0U;
static volatile uint16_t s_rxTail = 0U;

/**
 * @brief printf 重定向：把标准库输出的每个字符经蓝牙串口发出。
 * @param ch 要输出的字符。
 * @param f 文件指针（未使用）。
 * @retval 输出的字符。
 * @note 供 printf 等标准库函数调用，实现 printf 到蓝牙串口的重定向。
 */
int fputc(int ch, FILE *f)
{
	(void)f;
	Serial_SendByte((uint8_t)ch);
	return ch;
}

/**
 * @brief USART1 接收中断：RXNE 时把收到的字节写入接收环形缓冲。
 * @param 无。
 * @retval 无。
 * @note 缓冲满时丢弃最旧的一个字节，避免 head 追上 tail 导致死锁。
 */
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(SERIAL_USART, USART_IT_RXNE) != RESET)
	{
		/* 读 DR 会同时清除 RXNE 标志。 */
		s_rxBuffer[s_rxHead] = (uint8_t)USART_ReceiveData(SERIAL_USART);
		s_rxHead = (uint16_t)((s_rxHead + 1U) % SERIAL_RX_BUFFER_SIZE);

		/* 缓冲满（head 追上 tail）：丢弃最旧字节。 */
		if (s_rxHead == s_rxTail)
		{
			s_rxTail = (uint16_t)((s_rxTail + 1U) % SERIAL_RX_BUFFER_SIZE);
		}
	}
}

/**
 * @brief 初始化蓝牙串口（GPIO + USART1 + 接收中断）。
 * @param 无。
 * @retval 无。
 */
void Serial_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	/* 第一步：使能 GPIOA 和 USART1 时钟。 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

	/* 第二步：配置 TX=PA9 为复用推挽输出。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = SERIAL_TX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SERIAL_TX_PORT, &GPIO_InitStructure);

	/* 第三步：配置 RX=PA10 为浮空输入。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = SERIAL_RX_PIN;
	GPIO_Init(SERIAL_RX_PORT, &GPIO_InitStructure);

	/* 第四步：配置 USART1 为 9600、8 数据位、无校验、1 停止位。 */
	USART_InitStructure.USART_BaudRate = SERIAL_BAUD;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_Init(SERIAL_USART, &USART_InitStructure);

	/* 第五步：使能接收中断并配置 NVIC。 */
	USART_ITConfig(SERIAL_USART, USART_IT_RXNE, ENABLE);

	/* 中断优先级分组为系统级一次性设置，这里在唯一使用中断的模块中配置。 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	/* 第六步：使能 USART1。 */
	USART_Cmd(SERIAL_USART, ENABLE);
}

/**
 * @brief 阻塞发送一个字节。
 * @param Byte 要发送的字节。
 * @retval 无。
 */
void Serial_SendByte(uint8_t Byte)
{
	while (USART_GetFlagStatus(SERIAL_USART, USART_FLAG_TXE) == RESET)
	{
	}
	USART_SendData(SERIAL_USART, Byte);
}

/**
 * @brief 阻塞发送一个字节数组。
 * @param Array 数据首地址。
 * @param Length 数据长度（字节）。
 * @retval 无。
 */
void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i++)
	{
		Serial_SendByte(Array[i]);
	}
}

/**
 * @brief 阻塞发送一个以 '\0' 结尾的字符串。
 * @param String 字符串首地址。
 * @retval 无。
 */
void Serial_SendString(char *String)
{
	uint16_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		Serial_SendByte((uint8_t)String[i]);
	}
}

/**
 * @brief 阻塞发送数字（十进制，固定位数，高位补 0）。
 * @param Number 要发送的数字。
 * @param Length 发送的十进制位数。
 * @retval 无。
 */
void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t divisor = 1U;

	if (Length == 0U)
	{
		return;
	}

	/* 第一步：算出最高位对应的除数 10^(Length-1)。 */
	for (i = 0; i < (uint8_t)(Length - 1U); i++)
	{
		divisor *= 10U;
	}

	/* 第二步：从高位到低位逐位发送。 */
	for (i = 0; i < Length; i++)
	{
		Serial_SendByte((uint8_t)((Number / divisor) % 10U + (uint32_t)'0'));
		divisor /= 10U;
	}
}

/**
 * @brief 格式化发送（printf 风格，可直接使用 %d/%s 等）。
 * @param format 格式串。
 * @param ... 可变参数。
 * @retval 无。
 */
void Serial_Printf(char *format, ...)
{
	char String[160];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);
}

/**
 * @brief 查询接收缓冲中可读的字节数。
 * @param 无。
 * @retval 可用字节数。
 */
uint16_t Serial_Available(void)
{
	if (s_rxHead >= s_rxTail)
	{
		return (uint16_t)(s_rxHead - s_rxTail);
	}
	return (uint16_t)(SERIAL_RX_BUFFER_SIZE + s_rxHead - s_rxTail);
}

/**
 * @brief 从接收缓冲读取一个字节。
 * @param 无。
 * @retval 读取到的字节；缓冲为空时返回 0。
 */
uint8_t Serial_ReadByte(void)
{
	uint8_t data = 0U;

	if (s_rxHead != s_rxTail)
	{
		data = s_rxBuffer[s_rxTail];
		s_rxTail = (uint16_t)((s_rxTail + 1U) % SERIAL_RX_BUFFER_SIZE);
	}

	return data;
}
