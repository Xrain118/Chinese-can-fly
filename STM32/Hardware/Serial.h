#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_BAUD           (115200UL)
#define SERIAL_RX_BUFFER_SIZE (128U)

void Serial_Init(void);
void Serial_SendByte(uint8_t byte);
void Serial_SendArray(const uint8_t *array, uint16_t length);
void Serial_SendString(const char *string);
void Serial_SendNumber(uint32_t number, uint8_t length);
void Serial_Printf(const char *format, ...);
uint16_t Serial_Available(void);
uint8_t Serial_ReadByte(void);

#endif
