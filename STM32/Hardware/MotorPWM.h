#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdint.h>

/* 占空比统一使用千分比，和 MOTOR_SPEED_MAX / DRIVE_CONTROL_PWM_MAX 对齐。 */
#define MOTOR_PWM_DUTY_MAX (1000U)
/* 20 kHz 高于常见可闻频段，降低电机啸叫。 */
#define MOTOR_PWM_HZ       (20000UL)

/* channel 按车辆编号排列：Motor1=前左、Motor2=前右、Motor3=后左、Motor4=后右。 */
#define MOTOR_PWM_CHANNEL_MOTOR1 (1U)
#define MOTOR_PWM_CHANNEL_MOTOR2 (2U)
#define MOTOR_PWM_CHANNEL_MOTOR3 (3U)
#define MOTOR_PWM_CHANNEL_MOTOR4 (4U)

/* 初始化 PB8..PB11 上由 TIM10、TIM11、TIM2 生成的四路 PWM。 */
void MotorPWM_Init(void);
/* 设置指定 Motor 通道占空比，channel 超出 1..4 时忽略。 */
void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille);

#endif
