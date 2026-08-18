#ifndef ICM42688_H
#define ICM42688_H

#include <stdint.h>

#define ICM42688_WHO_AM_I_VALUE (0x47U)

/* 芯片原始样本，未做单位换算；ROS2 桥接会把六轴值转换成 SI 单位。 */
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

/* 初始化 SPI、INT1 和芯片寄存器；WHO_AM_I 或复位失败返回 0。 */
uint8_t ICM42688_Init(void);
/* 读取 WHO_AM_I，用于启动诊断。 */
uint8_t ICM42688_ReadWhoAmI(void);
/* 连续读取一帧温度、加速度和陀螺仪原始计数。 */
uint8_t ICM42688_ReadSample(ICM42688_RawSample *sample);
/* 读取并清除数据就绪标志；标志由 EXTI9_5_IRQHandler 置位。 */
uint8_t ICM42688_DataReady(void);

#endif
