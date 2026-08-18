#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdint.h>

/* 占空比统一使用千分比，和 MOTOR_SPEED_MAX / DRIVE_CONTROL_PWM_MAX 对齐。 */
#define MOTOR_PWM_DUTY_MAX (1000U)
/* 20 kHz 高于常见可闻频段，降低电机啸叫。 */
#define MOTOR_PWM_HZ       (20000UL)

/* 初始化 TIM5_CH1..CH4 和 PA0..PA3。 */
void MotorPWM_Init(void);
/* 设置指定通道占空比，channel 范围为 1..4，超出范围会被忽略。 */
void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille);

#endif
