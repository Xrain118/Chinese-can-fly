#include "Motor.h"
#include "MotorPWM.h"
#include "BoardPins.h"

static int16_t Motor_Clamp(int32_t command)
{
	if (command > MOTOR_SPEED_MAX)
	{
		return MOTOR_SPEED_MAX;
	}
	if (command < -MOTOR_SPEED_MAX)
	{
		return -MOTOR_SPEED_MAX;
	}
	return (int16_t)command;
}

static void Motor_WritePin(uint8_t pin, uint8_t high)
{
	if (high != 0U)
	{
		BOARD_MOTOR_PORT->BSRR = BOARD_PIN_MASK(pin);
	}
	else
	{
		BOARD_MOTOR_PORT->BSRR = (uint32_t)BOARD_PIN_MASK(pin) << 16U;
	}
}

static void Motor_SetDirection(int16_t command, uint8_t in1, uint8_t in2)
{
	if (command > 0)
	{
		Motor_WritePin(in1, 1U);
		Motor_WritePin(in2, 0U);
	}
	else if (command < 0)
	{
		Motor_WritePin(in1, 0U);
		Motor_WritePin(in2, 1U);
	}
	else
	{
		Motor_WritePin(in1, 0U);
		Motor_WritePin(in2, 0U);
	}
}

void Motor_Init(void)
{
	uint32_t pins = BOARD_PIN_MASK(BOARD_MOTOR_AIN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_AIN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_BIN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_BIN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_STBY_PIN);
	uint8_t pin;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
	(void)RCC->AHB1ENR;
	BOARD_MOTOR_PORT->BSRR = pins << 16U;
	for (pin = BOARD_MOTOR_BIN2_PIN; pin <= BOARD_MOTOR_STBY_PIN; pin++)
	{
		BOARD_MOTOR_PORT->MODER = (BOARD_MOTOR_PORT->MODER & ~(3UL << (pin * 2U))) |
								(1UL << (pin * 2U));
		BOARD_MOTOR_PORT->PUPDR &= ~(3UL << (pin * 2U));
		BOARD_MOTOR_PORT->OSPEEDR |= 2UL << (pin * 2U);
	}
	BOARD_MOTOR_PORT->OTYPER &= ~pins;

	MotorPWM_Init();
	Motor_Stop();
}

void Motor_Stop(void)
{
	MotorPWM_SetDuty(1U, 0U);
	MotorPWM_SetDuty(2U, 0U);
	Motor_SetDirection(0, BOARD_MOTOR_AIN1_PIN, BOARD_MOTOR_AIN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_BIN1_PIN, BOARD_MOTOR_BIN2_PIN);
	Motor_WritePin(BOARD_MOTOR_STBY_PIN, 0U);
}

void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed)
{
	int16_t left = Motor_Clamp((int32_t)leftSpeed * MOTOR_LEFT_DIRECTION_SIGN);
	int16_t right = Motor_Clamp((int32_t)rightSpeed * MOTOR_RIGHT_DIRECTION_SIGN);
	uint16_t leftDuty;
	uint16_t rightDuty;

	if ((left == 0) && (right == 0))
	{
		Motor_Stop();
		return;
	}

	Motor_WritePin(BOARD_MOTOR_STBY_PIN, 0U);
	Motor_SetDirection(left, BOARD_MOTOR_AIN1_PIN, BOARD_MOTOR_AIN2_PIN);
	Motor_SetDirection(right, BOARD_MOTOR_BIN1_PIN, BOARD_MOTOR_BIN2_PIN);
	leftDuty = (uint16_t)((left < 0) ? -left : left);
	rightDuty = (uint16_t)((right < 0) ? -right : right);
	MotorPWM_SetDuty(1U, leftDuty);
	MotorPWM_SetDuty(2U, rightDuty);
	Motor_WritePin(BOARD_MOTOR_STBY_PIN, 1U);
}
