/*
 * TB6612 四轮电机驱动封装。
 *
 * DriveControl 给的是逻辑左右侧 PWM，本层负责应用每个轮子的方向校准符号、
 * 设置 IN1/IN2 方向脚和 TIM5 PWM 占空比。零速时拉低两侧 STBY，保证停车
 * 路径尽量直接、确定。
 */
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
		BOARD_MOTOR_CTRL_PORT->BSRR = BOARD_PIN_MASK(pin);
	}
	else
	{
		BOARD_MOTOR_CTRL_PORT->BSRR = (uint32_t)BOARD_PIN_MASK(pin) << 16U;
	}
}

static void Motor_SetDirection(int16_t command, uint8_t in1, uint8_t in2)
{
	/* TB6612 方向由 IN1/IN2 决定；PWM 只表示幅值，正负号在这里消化。 */
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

static void Motor_InitOutputPin(uint8_t pin)
{
	BOARD_MOTOR_CTRL_PORT->MODER =
		(BOARD_MOTOR_CTRL_PORT->MODER & ~(3UL << (pin * 2U))) |
		(1UL << (pin * 2U));
	BOARD_MOTOR_CTRL_PORT->PUPDR &= ~(3UL << (pin * 2U));
	BOARD_MOTOR_CTRL_PORT->OSPEEDR |= 2UL << (pin * 2U);
}

void Motor_Init(void)
{
	uint32_t pins = BOARD_PIN_MASK(BOARD_MOTOR_LF_IN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_LF_IN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_LR_IN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_LR_IN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_LEFT_STBY_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RF_IN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RF_IN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RR_IN1_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RR_IN2_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RIGHT_STBY_PIN);

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
	(void)RCC->AHB1ENR;
	BOARD_MOTOR_CTRL_PORT->BSRR = pins << 16U;
	Motor_InitOutputPin(BOARD_MOTOR_LF_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LF_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LR_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LR_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LEFT_STBY_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RF_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RF_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RR_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RR_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RIGHT_STBY_PIN);
	BOARD_MOTOR_CTRL_PORT->OTYPER &= ~pins;

	MotorPWM_Init();
	Motor_Stop();
}

void Motor_Stop(void)
{
	MotorPWM_SetDuty(1U, 0U);
	MotorPWM_SetDuty(2U, 0U);
	MotorPWM_SetDuty(3U, 0U);
	MotorPWM_SetDuty(4U, 0U);
	Motor_SetDirection(0, BOARD_MOTOR_LF_IN1_PIN, BOARD_MOTOR_LF_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_LR_IN1_PIN, BOARD_MOTOR_LR_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_RF_IN1_PIN, BOARD_MOTOR_RF_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_RR_IN1_PIN, BOARD_MOTOR_RR_IN2_PIN);
	Motor_WritePin(BOARD_MOTOR_LEFT_STBY_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RIGHT_STBY_PIN, 0U);
}

void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed)
{
	Motor_SetWheelSpeeds(leftSpeed, leftSpeed, rightSpeed, rightSpeed);
}

void Motor_SetWheelSpeeds(int16_t leftFrontSpeed, int16_t leftRearSpeed,
						  int16_t rightFrontSpeed, int16_t rightRearSpeed)
{
	/* 四个方向符号是现场校准点：改线或换电机后优先检查 Motor.h 中的 SIGN。 */
	int16_t lf = Motor_Clamp((int32_t)leftFrontSpeed * MOTOR_LEFT_FRONT_DIRECTION_SIGN);
	int16_t lr = Motor_Clamp((int32_t)leftRearSpeed * MOTOR_LEFT_REAR_DIRECTION_SIGN);
	int16_t rf = Motor_Clamp((int32_t)rightFrontSpeed * MOTOR_RIGHT_FRONT_DIRECTION_SIGN);
	int16_t rr = Motor_Clamp((int32_t)rightRearSpeed * MOTOR_RIGHT_REAR_DIRECTION_SIGN);
	uint16_t lfDuty;
	uint16_t lrDuty;
	uint16_t rfDuty;
	uint16_t rrDuty;

	if ((lf == 0) && (lr == 0) && (rf == 0) && (rr == 0))
	{
		Motor_Stop();
		return;
	}

	/* 先关 STBY 再改方向/占空比，避免 TB6612 在切向瞬间打出毛刺。 */
	Motor_WritePin(BOARD_MOTOR_LEFT_STBY_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RIGHT_STBY_PIN, 0U);
	Motor_SetDirection(lf, BOARD_MOTOR_LF_IN1_PIN, BOARD_MOTOR_LF_IN2_PIN);
	Motor_SetDirection(lr, BOARD_MOTOR_LR_IN1_PIN, BOARD_MOTOR_LR_IN2_PIN);
	Motor_SetDirection(rf, BOARD_MOTOR_RF_IN1_PIN, BOARD_MOTOR_RF_IN2_PIN);
	Motor_SetDirection(rr, BOARD_MOTOR_RR_IN1_PIN, BOARD_MOTOR_RR_IN2_PIN);
	lfDuty = (uint16_t)((lf < 0) ? -lf : lf);
	lrDuty = (uint16_t)((lr < 0) ? -lr : lr);
	rfDuty = (uint16_t)((rf < 0) ? -rf : rf);
	rrDuty = (uint16_t)((rr < 0) ? -rr : rr);
	MotorPWM_SetDuty(1U, lfDuty);
	MotorPWM_SetDuty(2U, lrDuty);
	MotorPWM_SetDuty(3U, rfDuty);
	MotorPWM_SetDuty(4U, rrDuty);
	Motor_WritePin(BOARD_MOTOR_LEFT_STBY_PIN, 1U);
	Motor_WritePin(BOARD_MOTOR_RIGHT_STBY_PIN, 1U);
}
