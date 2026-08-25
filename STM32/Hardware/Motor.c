/*
 * TB6612 四轮电机驱动封装。
 *
 * DriveControl 给的是逻辑左右侧 PWM，本层负责应用每个轮子的方向校准符号、
 * 设置两块外接 TB6612 的 IN1/IN2、PWM 和前/后 STBY。Motor1..4 依次为
 * 前左、前右、后左、后右。
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

static void Motor_WritePin(GPIO_TypeDef *port, uint8_t pin, uint8_t high)
{
	if (high != 0U)
	{
		port->BSRR = BOARD_PIN_MASK(pin);
	}
	else
	{
		port->BSRR = (uint32_t)BOARD_PIN_MASK(pin) << 16U;
	}
}

static void Motor_SetDirection(int16_t command, GPIO_TypeDef *port,
							   uint8_t in1, uint8_t in2)
{
	/* TB6612 方向由 IN1/IN2 决定；PWM 只表示幅值，正负号在这里消化。 */
	if (command > 0)
	{
		Motor_WritePin(port, in1, 1U);
		Motor_WritePin(port, in2, 0U);
	}
	else if (command < 0)
	{
		Motor_WritePin(port, in1, 0U);
		Motor_WritePin(port, in2, 1U);
	}
	else
	{
		Motor_WritePin(port, in1, 0U);
		Motor_WritePin(port, in2, 0U);
	}
}

static void Motor_InitOutputPin(GPIO_TypeDef *port, uint8_t pin)
{
	uint32_t modeMask = 3UL << (pin * 2U);
	port->MODER =
		(port->MODER & ~modeMask) |
		(1UL << (pin * 2U));
	port->PUPDR &= ~modeMask;
	port->OSPEEDR = (port->OSPEEDR & ~modeMask) | (2UL << (pin * 2U));
	port->OTYPER &= ~BOARD_PIN_MASK(pin);
}

void Motor_Init(void)
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN |
					RCC_AHB1ENR_GPIOEEN;
	(void)RCC->AHB1ENR;

	/* 先锁定所有方向脚和两块模块的 STBY 为低，再切换成输出模式。 */
	Motor_WritePin(BOARD_MOTOR_LF_CTRL_PORT, BOARD_MOTOR_LF_IN1_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_LF_CTRL_PORT, BOARD_MOTOR_LF_IN2_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RF_CTRL_PORT, BOARD_MOTOR_RF_IN1_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RF_CTRL_PORT, BOARD_MOTOR_RF_IN2_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_FRONT_STBY_PORT, BOARD_MOTOR_FRONT_STBY_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_LR_CTRL_PORT, BOARD_MOTOR_LR_IN1_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_LR_CTRL_PORT, BOARD_MOTOR_LR_IN2_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RR_CTRL_PORT, BOARD_MOTOR_RR_IN1_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_RR_CTRL_PORT, BOARD_MOTOR_RR_IN2_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_REAR_STBY_PORT, BOARD_MOTOR_REAR_STBY_PIN, 0U);

	Motor_InitOutputPin(BOARD_MOTOR_LF_CTRL_PORT, BOARD_MOTOR_LF_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LF_CTRL_PORT, BOARD_MOTOR_LF_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RF_CTRL_PORT, BOARD_MOTOR_RF_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RF_CTRL_PORT, BOARD_MOTOR_RF_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_FRONT_STBY_PORT, BOARD_MOTOR_FRONT_STBY_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LR_CTRL_PORT, BOARD_MOTOR_LR_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_LR_CTRL_PORT, BOARD_MOTOR_LR_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RR_CTRL_PORT, BOARD_MOTOR_RR_IN1_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_RR_CTRL_PORT, BOARD_MOTOR_RR_IN2_PIN);
	Motor_InitOutputPin(BOARD_MOTOR_REAR_STBY_PORT, BOARD_MOTOR_REAR_STBY_PIN);

	MotorPWM_Init();
	Motor_Stop();
}

void Motor_Stop(void)
{
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR1, 0U);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR2, 0U);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR3, 0U);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR4, 0U);
	Motor_SetDirection(0, BOARD_MOTOR_LF_CTRL_PORT,
					   BOARD_MOTOR_LF_IN1_PIN, BOARD_MOTOR_LF_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_RF_CTRL_PORT,
					   BOARD_MOTOR_RF_IN1_PIN, BOARD_MOTOR_RF_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_LR_CTRL_PORT,
					   BOARD_MOTOR_LR_IN1_PIN, BOARD_MOTOR_LR_IN2_PIN);
	Motor_SetDirection(0, BOARD_MOTOR_RR_CTRL_PORT,
					   BOARD_MOTOR_RR_IN1_PIN, BOARD_MOTOR_RR_IN2_PIN);
	Motor_WritePin(BOARD_MOTOR_FRONT_STBY_PORT, BOARD_MOTOR_FRONT_STBY_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_REAR_STBY_PORT, BOARD_MOTOR_REAR_STBY_PIN, 0U);
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
	uint8_t frontActive;
	uint8_t rearActive;

	if ((lf == 0) && (lr == 0) && (rf == 0) && (rr == 0))
	{
		Motor_Stop();
		return;
	}

	/* 先关两块模块的 STBY，再改方向/占空比，避免切向瞬间打出毛刺。 */
	Motor_WritePin(BOARD_MOTOR_FRONT_STBY_PORT, BOARD_MOTOR_FRONT_STBY_PIN, 0U);
	Motor_WritePin(BOARD_MOTOR_REAR_STBY_PORT, BOARD_MOTOR_REAR_STBY_PIN, 0U);
	Motor_SetDirection(lf, BOARD_MOTOR_LF_CTRL_PORT,
					   BOARD_MOTOR_LF_IN1_PIN, BOARD_MOTOR_LF_IN2_PIN);
	Motor_SetDirection(rf, BOARD_MOTOR_RF_CTRL_PORT,
					   BOARD_MOTOR_RF_IN1_PIN, BOARD_MOTOR_RF_IN2_PIN);
	Motor_SetDirection(lr, BOARD_MOTOR_LR_CTRL_PORT,
					   BOARD_MOTOR_LR_IN1_PIN, BOARD_MOTOR_LR_IN2_PIN);
	Motor_SetDirection(rr, BOARD_MOTOR_RR_CTRL_PORT,
					   BOARD_MOTOR_RR_IN1_PIN, BOARD_MOTOR_RR_IN2_PIN);
	lfDuty = (uint16_t)((lf < 0) ? -lf : lf);
	lrDuty = (uint16_t)((lr < 0) ? -lr : lr);
	rfDuty = (uint16_t)((rf < 0) ? -rf : rf);
	rrDuty = (uint16_t)((rr < 0) ? -rr : rr);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR1, lfDuty);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR2, rfDuty);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR3, lrDuty);
	MotorPWM_SetDuty(MOTOR_PWM_CHANNEL_MOTOR4, rrDuty);
	frontActive = ((lf != 0) || (rf != 0)) ? 1U : 0U;
	rearActive = ((lr != 0) || (rr != 0)) ? 1U : 0U;
	Motor_WritePin(BOARD_MOTOR_FRONT_STBY_PORT, BOARD_MOTOR_FRONT_STBY_PIN,
				   frontActive);
	Motor_WritePin(BOARD_MOTOR_REAR_STBY_PORT, BOARD_MOTOR_REAR_STBY_PIN,
				   rearActive);
}
