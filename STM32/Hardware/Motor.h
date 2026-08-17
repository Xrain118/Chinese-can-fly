#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#define MOTOR_SPEED_MAX (1000)

/* Change only these signs if the assembled motor polarity is reversed. */
#define MOTOR_LEFT_DIRECTION_SIGN  (1)
#define MOTOR_RIGHT_DIRECTION_SIGN (1)

void Motor_Init(void);
void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed);
void Motor_Stop(void);

#endif
