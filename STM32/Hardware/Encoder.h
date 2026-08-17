#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f10x.h"

/*
 * Four encoder placeholder interface.
 *
 * Contract:
 * - Counts increase when the car moves forward.
 * - Left front/rear are averaged by DriveControl as the left wheel speed.
 * - Right front/rear are averaged by DriveControl as the right wheel speed.
 *
 * Real pin and TIM/EXTI setup is intentionally not implemented yet. After the
 * encoder wiring is fixed, fill Encoder_Init() and the interrupt handlers in
 * Encoder.c, then update only the sign macros if a channel is reversed.
 */

void Encoder_Init(void);

int32_t Encoder_GetLeftFrontCount(void);
int32_t Encoder_GetLeftRearCount(void);
int32_t Encoder_GetRightFrontCount(void);
int32_t Encoder_GetRightRearCount(void);

void Encoder_SetSimulatedCounts(int32_t leftFront, int32_t leftRear,
								int32_t rightFront, int32_t rightRear);

#endif
