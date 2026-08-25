#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

/* 车端 PWM 命令范围，与网页和 ROS2 桥接保持一致。 */
#define MOTOR_SPEED_MAX (1000)

/*
 * Motor1=左前、Motor2=右前、Motor3=左后、Motor4=右后。
 * 现场校准时如果某个轮子前进方向反了，只改对应符号，不改控制算法。
 */
#define MOTOR_LEFT_FRONT_DIRECTION_SIGN  (1)
#define MOTOR_LEFT_REAR_DIRECTION_SIGN   (1)
#define MOTOR_RIGHT_FRONT_DIRECTION_SIGN (-1)
#define MOTOR_RIGHT_REAR_DIRECTION_SIGN  (-1)

/* 初始化两块外接 TB6612 的 GPIO/PWM，并保持四个电机停车。 */
void Motor_Init(void);
/* 设置左右侧同速输出；当前控制器同侧两轮共用一个目标。 */
void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed);
/* 预留四轮独立输出接口；当前上层主要使用左右侧封装。 */
void Motor_SetWheelSpeeds(int16_t leftFrontSpeed, int16_t leftRearSpeed,
						  int16_t rightFrontSpeed, int16_t rightRearSpeed);
/* 清零占空比、方向脚和两侧 STBY。 */
void Motor_Stop(void);

#endif
