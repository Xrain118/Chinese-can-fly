#ifndef __SERIAL_H
#define __SERIAL_H

#include "stm32f10x.h"                  // Device header

/**
 * @brief HC-04/HC-05 蓝牙模块透传驱动（USART1）。
 *
 * 蓝牙模块直接插在最小系统板的 TX/RX 上，即 USART1：TX=PA9、RX=PA10，
 * 波特率 9600、8 数据位、无校验、1 停止位。HC-04/HC-05 出厂默认 9600，
 * 手机蓝牙串口助手按同样参数即可收发。
 *
 * 本驱动提供：
 * - 阻塞式发送：Serial_SendByte / SendArray / SendString / SendNumber / Printf，
 *   以及 printf 重定向（见 Serial.c 中的 fputc）；
 * - 中断接收：RXNE 中断把数据写入 64 字节环形缓冲，主循环用
 *   Serial_Available() / Serial_ReadByte() 取出。
 */

/* 蓝牙串口外设与引脚配置。 */
#define SERIAL_USART          USART1
#define SERIAL_BAUD           (9600U)
#define SERIAL_TX_PORT        GPIOA
#define SERIAL_TX_PIN         GPIO_Pin_9
#define SERIAL_RX_PORT        GPIOA
#define SERIAL_RX_PIN         GPIO_Pin_10

/* 接收环形缓冲大小（2 的幂，便于取模）。 */
#define SERIAL_RX_BUFFER_SIZE (64U)

/**
 * @brief 初始化蓝牙串口（GPIO + USART1 + 接收中断）。
 * @param 无。
 * @retval 无。
 * @note 初始化后 USART1 使能，RXNE 接收中断使能，可随时收发。
 */
void Serial_Init(void);

/**
 * @brief 阻塞发送一个字节。
 * @param Byte 要发送的字节。
 * @retval 无。
 */
void Serial_SendByte(uint8_t Byte);

/**
 * @brief 阻塞发送一个字节数组。
 * @param Array 数据首地址。
 * @param Length 数据长度（字节）。
 * @retval 无。
 */
void Serial_SendArray(uint8_t *Array, uint16_t Length);

/**
 * @brief 阻塞发送一个以 '\0' 结尾的字符串。
 * @param String 字符串首地址。
 * @retval 无。
 */
void Serial_SendString(char *String);

/**
 * @brief 阻塞发送数字（十进制，固定位数，高位补 0）。
 * @param Number 要发送的数字。
 * @param Length 发送的十进制位数。
 * @retval 无。
 */
void Serial_SendNumber(uint32_t Number, uint8_t Length);

/**
 * @brief 格式化发送（printf 风格，可直接使用 %d/%s 等）。
 * @param format 格式串。
 * @param ... 可变参数。
 * @retval 无。
 */
void Serial_Printf(char *format, ...);

/**
 * @brief 查询接收缓冲中可读的字节数。
 * @param 无。
 * @retval 可用字节数。
 */
uint16_t Serial_Available(void);

/**
 * @brief 从接收缓冲读取一个字节。
 * @param 无。
 * @retval 读取到的字节；缓冲为空时返回 0。
 */
uint8_t Serial_ReadByte(void);

#endif