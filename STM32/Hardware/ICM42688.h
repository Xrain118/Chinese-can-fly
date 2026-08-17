#ifndef ICM42688_H
#define ICM42688_H

#include <stdint.h>

#define ICM42688_WHO_AM_I_VALUE (0x47U)

typedef struct
{
	int16_t temperature;
	int16_t accelX;
	int16_t accelY;
	int16_t accelZ;
	int16_t gyroX;
	int16_t gyroY;
	int16_t gyroZ;
} ICM42688_RawSample;

uint8_t ICM42688_Init(void);
uint8_t ICM42688_ReadWhoAmI(void);
uint8_t ICM42688_ReadSample(ICM42688_RawSample *sample);
uint8_t ICM42688_DataReady(void);

#endif
