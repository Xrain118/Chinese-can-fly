#include "Motor.h"
#include "MotorPWM.h"

/** 只在本文件内部使用的左右电机编号。 */
typedef enum
{
	MOTOR_LEFT = 0,
	MOTOR_RIGHT = 1
} Motor_Side;

static void Motor_SetMotor(Motor_Side side, int16_t speed);

/**
 * @brief 查询指定侧的 IN1/IN2 引脚配置。
 * @param side 左/右电机侧。
 * @param in1Port 输出：IN1 引脚所在端口。
 * @param in1Pin 输出：IN1 引脚。
 * @param in2Port 输出：IN2 引脚所在端口。
 * @param in2Pin 输出：IN2 引脚。
 * @retval 无。
 */
static void Motor_GetDirPins(Motor_Side side, GPIO_TypeDef** in1Port, uint16_t* in1Pin,
							 GPIO_TypeDef** in2Port, uint16_t* in2Pin)
{
	if (side == MOTOR_LEFT)
	{
		*in1Port = MOTOR_LEFT_IN1_PORT;
		*in1Pin = MOTOR_LEFT_IN1_PIN;
		*in2Port = MOTOR_LEFT_IN2_PORT;
		*in2Pin = MOTOR_LEFT_IN2_PIN;
	}
	else
	{
		*in1Port = MOTOR_RIGHT_IN1_PORT;
		*in1Pin = MOTOR_RIGHT_IN1_PIN;
		*in2Port = MOTOR_RIGHT_IN2_PORT;
		*in2Pin = MOTOR_RIGHT_IN2_PIN;
	}
}

/**
 * @brief 初始化左右方向引脚（并调用 MotorPWM_Init 初始化 PWM）。
 * @param 无。
 * @retval 无。
 */
void Motor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 第一步：先初始化 PWM。 */
	MotorPWM_Init();

	/* 第二步：使能方向引脚所在端口的时钟。 */
	RCC_APB2PeriphClockCmd(MOTOR_DIR_RCC, ENABLE);

	/* 第三步：配置四个方向引脚（AIN1/AIN2、BIN1/BIN2）为推挽输出。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = MOTOR_LEFT_IN1_PIN;
	GPIO_Init(MOTOR_LEFT_IN1_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_LEFT_IN2_PIN;
	GPIO_Init(MOTOR_LEFT_IN2_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_RIGHT_IN1_PIN;
	GPIO_Init(MOTOR_RIGHT_IN1_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = MOTOR_RIGHT_IN2_PIN;
	GPIO_Init(MOTOR_RIGHT_IN2_PORT, &GPIO_InitStructure);
}

/**
 * @brief 设置指定侧的电机速度。
 * @param side 左/右电机侧。
 * @param speed 有符号速度命令，-1000~+1000：正转/反转/停止。
 * @retval 无。
 */
static void Motor_SetMotor(Motor_Side side, int16_t speed)
{
	int32_t command = speed;
	uint16_t duty;
	GPIO_TypeDef* in1Port;
	uint16_t in1Pin;
	GPIO_TypeDef* in2Port;
	uint16_t in2Pin;

	/* 第一步：把速度命令限制到 -1000~+1000。 */
	if (command > MOTOR_SPEED_MAX)
	{
		command = MOTOR_SPEED_MAX;
	}
	else if (command < -MOTOR_SPEED_MAX)
	{
		command = -MOTOR_SPEED_MAX;
	}

	/* 第二步：由符号写方向引脚。 */
	Motor_GetDirPins(side, &in1Port, &in1Pin, &in2Port, &in2Pin);
	if (command > 0)
	{
		GPIO_SetBits(in1Port, in1Pin);
		GPIO_ResetBits(in2Port, in2Pin);
	}
	else if (command < 0)
	{
		GPIO_ResetBits(in1Port, in1Pin);
		GPIO_SetBits(in2Port, in2Pin);
	}
	else
	{
		GPIO_ResetBits(in1Port, in1Pin);
		GPIO_ResetBits(in2Port, in2Pin);
	}

	/* 第三步：速度绝对值作为 PWM 占空比（左侧=通道1，右侧=通道2）。 */
	duty = (uint16_t)((command < 0) ? -command : command);
	if (side == MOTOR_LEFT)
	{
		MotorPWM_SetDuty(1U, duty);
	}
	else
	{
		MotorPWM_SetDuty(2U, duty);
	}
}

/**
 * @brief 同时设置左右两路电机速度。
 * @param leftSpeed 左侧（A 通道）有符号速度命令。
 * @param rightSpeed 右侧（B 通道）有符号速度命令。
 * @retval 无。
 */
void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed)
{
	Motor_SetMotor(MOTOR_LEFT, leftSpeed);
	Motor_SetMotor(MOTOR_RIGHT, rightSpeed);
}