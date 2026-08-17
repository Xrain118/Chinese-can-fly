#include "Encoder.h"

#define ENCODER_LEFT_FRONT_SIGN   (1)
#define ENCODER_LEFT_REAR_SIGN    (1)
#define ENCODER_RIGHT_FRONT_SIGN  (1)
#define ENCODER_RIGHT_REAR_SIGN   (1)

static volatile int32_t g_leftFrontCount = 0;
static volatile int32_t g_leftRearCount = 0;
static volatile int32_t g_rightFrontCount = 0;
static volatile int32_t g_rightRearCount = 0;

void Encoder_Init(void)
{
	/*
	 * Hardware TODO, kept as comments so this logic layer does not depend on
	 * unassigned pins yet.
	 *
	 * Suggested implementation:
	 * - Configure four AB encoder inputs.
	 * - Prefer TIM encoder mode for timers that have both channels available.
	 * - Use EXTI + quadrature table for the remaining channels if timers are
	 *   not enough.
	 * - Normalize count signs so forward motion is positive.
	 */
	g_leftFrontCount = 0;
	g_leftRearCount = 0;
	g_rightFrontCount = 0;
	g_rightRearCount = 0;
}

int32_t Encoder_GetLeftFrontCount(void)
{
	return g_leftFrontCount * ENCODER_LEFT_FRONT_SIGN;
}

int32_t Encoder_GetLeftRearCount(void)
{
	return g_leftRearCount * ENCODER_LEFT_REAR_SIGN;
}

int32_t Encoder_GetRightFrontCount(void)
{
	return g_rightFrontCount * ENCODER_RIGHT_FRONT_SIGN;
}

int32_t Encoder_GetRightRearCount(void)
{
	return g_rightRearCount * ENCODER_RIGHT_REAR_SIGN;
}

void Encoder_SetSimulatedCounts(int32_t leftFront, int32_t leftRear,
								int32_t rightFront, int32_t rightRear)
{
	g_leftFrontCount = leftFront;
	g_leftRearCount = leftRear;
	g_rightFrontCount = rightFront;
	g_rightRearCount = rightRear;
}
