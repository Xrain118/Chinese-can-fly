#include "MotorPWM.h"

/**
 * @brief 初始化双路电机 PWM。
 * @param 无。
 * @retval 无。
 */
void MotorPWM_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	/* 第一步：使能定时器与 PWM 引脚所在端口的时钟。 */
	RCC_APB1PeriphClockCmd(MOTOR_PWM_RCC, ENABLE);
	RCC_APB2PeriphClockCmd(MOTOR_PWM_GPIO_RCC, ENABLE);

	/* 第二步：配置两个 PWM 引脚为复用推挽输出。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = MOTOR_PWM_CH1_PIN | MOTOR_PWM_CH2_PIN;
	GPIO_Init(MOTOR_PWM_GPIO_PORT, &GPIO_InitStructure);

	/* 第三步：配置时基。 */
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period = MOTOR_PWM_PERIOD;
	TIM_TimeBaseStructure.TIM_Prescaler = MOTOR_PWM_PRESCALER;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(MOTOR_PWM_TIM, &TIM_TimeBaseStructure);

	/* 第四步：配置两个比较输出通道为 PWM1 模式，初始占空比为 0。 */
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse = 0;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC1Init(MOTOR_PWM_TIM, &TIM_OCInitStructure);
	TIM_OC2Init(MOTOR_PWM_TIM, &TIM_OCInitStructure);

	/* 第五步：启动定时器，输出两路 0% PWM。 */
	TIM_Cmd(MOTOR_PWM_TIM, ENABLE);
}

/**
 * @brief 设置指定通道的 PWM 占空比。
 * @param channel 通道编号，范围 1~2。
 * @param dutyPermille 占空比千分数，0~1000；超过 1000 时自动限幅。
 * @retval 无。
 */
void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille)
{
	/* 第一步：将占空比限制到 0%~100%。 */
	if (dutyPermille > MOTOR_PWM_DUTY_MAX)
	{
		dutyPermille = MOTOR_PWM_DUTY_MAX;
	}

	/* 第二步：占空比直接作为比较值写入对应通道。 */
	switch (channel)
	{
	case 1U: TIM_SetCompare1(MOTOR_PWM_TIM, dutyPermille); break;
	case 2U: TIM_SetCompare2(MOTOR_PWM_TIM, dutyPermille); break;
	default: break;
	}
}