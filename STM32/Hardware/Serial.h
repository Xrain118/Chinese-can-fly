#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_BAUD           (115200UL)
#define SERIAL_RX_BUFFER_SIZE (128U)

/* 初始化 USART2 PD5/PD6 和 RX 中断。 */
void Serial_Init(void);
/* 阻塞发送 1 字节；只允许 SerialTxTask 间接调用。 */
void Serial_SendByte(uint8_t byte);
/* 阻塞发送数组。 */
void Serial_SendArray(const uint8_t *array, uint16_t length);
/* 阻塞发送 C 字符串，不自动补 CRLF。 */
void Serial_SendString(const char *string);
/* 发送固定宽度十进制数字，保留旧调试接口。 */
void Serial_SendNumber(uint32_t number, uint8_t length);
/* 查询 RX 环形缓冲中可读字节数。 */
uint16_t Serial_Available(void);
/* 从 RX 环形缓冲读取 1 字节；空缓冲返回 0。 */
uint8_t Serial_ReadByte(void);

#endif
