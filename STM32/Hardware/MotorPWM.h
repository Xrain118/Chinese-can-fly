#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdint.h>

#define MOTOR_PWM_DUTY_MAX (1000U)
#define MOTOR_PWM_HZ       (20000UL)

void MotorPWM_Init(void);
void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille);

#endif
