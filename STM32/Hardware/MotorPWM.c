/*
 * 两块外接 TB6612 的四路 PWM 底层。
 *
 * Motor1/Motor2（前左/前右）分别使用 TIM10_CH1、TIM11_CH1；
 * Motor3/Motor4（后左/后右）使用 TIM2_CH3、TIM2_CH4。所有输出均为
 * 20 kHz，上层统一使用 0..1000 的千分比占空比。
 */
#include "MotorPWM.h"
#include "BoardPins.h"
#include "BoardClock.h"

#define MOTOR_PWM_APB1_PERIOD_COUNTS (BOARD_APB1_TIMER_HZ / MOTOR_PWM_HZ)
#define MOTOR_PWM_APB2_PERIOD_COUNTS (BOARD_APB2_TIMER_HZ / MOTOR_PWM_HZ)

typedef struct
{
	TIM_TypeDef *timer;
	uint8_t timerChannel;
	uint32_t periodCounts;
} MotorPWM_Output;

/* 数组顺序就是对外 channel 1..4，即 Motor1..Motor4。 */
static const MotorPWM_Output g_motorOutputs[4] =
{
	{BOARD_MOTOR_LF_PWM_TIM, BOARD_MOTOR_LF_PWM_CHANNEL, MOTOR_PWM_APB2_PERIOD_COUNTS},
	{BOARD_MOTOR_RF_PWM_TIM, BOARD_MOTOR_RF_PWM_CHANNEL, MOTOR_PWM_APB2_PERIOD_COUNTS},
	{BOARD_MOTOR_LR_PWM_TIM, BOARD_MOTOR_LR_PWM_CHANNEL, MOTOR_PWM_APB1_PERIOD_COUNTS},
	{BOARD_MOTOR_RR_PWM_TIM, BOARD_MOTOR_RR_PWM_CHANNEL, MOTOR_PWM_APB1_PERIOD_COUNTS}
};

static void MotorPWM_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

static void MotorPWM_InitPin(uint8_t pin, uint8_t af)
{
	uint32_t modeMask = 3UL << (pin * 2U);
	uint32_t pinMask = BOARD_PIN_MASK(pin);

	BOARD_MOTOR_PWM_PORT->MODER =
		(BOARD_MOTOR_PWM_PORT->MODER & ~modeMask) | (2UL << (pin * 2U));
	BOARD_MOTOR_PWM_PORT->OTYPER &= ~pinMask;
	BOARD_MOTOR_PWM_PORT->OSPEEDR =
		(BOARD_MOTOR_PWM_PORT->OSPEEDR & ~modeMask) | (2UL << (pin * 2U));
	BOARD_MOTOR_PWM_PORT->PUPDR &= ~modeMask;
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PWM_PORT, pin, af);
}

static void MotorPWM_InitTimer(TIM_TypeDef *timer, uint32_t periodCounts)
{
	timer->CR1 = 0U;
	timer->CR2 = 0U;
	timer->SMCR = 0U;
	timer->DIER = 0U;
	timer->CCER = 0U;
	timer->CCMR1 = 0U;
	timer->CCMR2 = 0U;
	timer->PSC = 0U;
	timer->ARR = periodCounts - 1UL;
	timer->CNT = 0U;
	timer->CCR1 = 0U;
	timer->CCR2 = 0U;
	timer->CCR3 = 0U;
	timer->CCR4 = 0U;
}

static void MotorPWM_EnableOutput(TIM_TypeDef *timer, uint8_t timerChannel)
{
	if (timerChannel == 1U)
	{
		timer->CCMR1 |= TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
		timer->CCER |= TIM_CCER_CC1E;
	}
	else if (timerChannel == 2U)
	{
		timer->CCMR1 |= TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
		timer->CCER |= TIM_CCER_CC2E;
	}
	else if (timerChannel == 3U)
	{
		timer->CCMR2 |= TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2;
		timer->CCER |= TIM_CCER_CC3E;
	}
	else if (timerChannel == 4U)
	{
		timer->CCMR2 |= TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2;
		timer->CCER |= TIM_CCER_CC4E;
	}
}

static void MotorPWM_StartTimer(TIM_TypeDef *timer)
{
	timer->EGR = TIM_EGR_UG;
	timer->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void MotorPWM_WriteCompare(TIM_TypeDef *timer, uint8_t timerChannel,
								  uint32_t compare)
{
	if (timerChannel == 1U)
	{
		timer->CCR1 = compare;
	}
	else if (timerChannel == 2U)
	{
		timer->CCR2 = compare;
	}
	else if (timerChannel == 3U)
	{
		timer->CCR3 = compare;
	}
	else if (timerChannel == 4U)
	{
		timer->CCR4 = compare;
	}
}

void MotorPWM_Init(void)
{
	uint8_t index;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM10EN | RCC_APB2ENR_TIM11EN;
	(void)RCC->APB2ENR;

	MotorPWM_InitPin(BOARD_MOTOR_LF_PWM_PIN, BOARD_MOTOR_LF_PWM_AF);
	MotorPWM_InitPin(BOARD_MOTOR_RF_PWM_PIN, BOARD_MOTOR_RF_PWM_AF);
	MotorPWM_InitPin(BOARD_MOTOR_LR_PWM_PIN, BOARD_MOTOR_LR_PWM_AF);
	MotorPWM_InitPin(BOARD_MOTOR_RR_PWM_PIN, BOARD_MOTOR_RR_PWM_AF);

	MotorPWM_InitTimer(BOARD_MOTOR_LF_PWM_TIM, MOTOR_PWM_APB2_PERIOD_COUNTS);
	MotorPWM_InitTimer(BOARD_MOTOR_RF_PWM_TIM, MOTOR_PWM_APB2_PERIOD_COUNTS);
	MotorPWM_InitTimer(BOARD_MOTOR_LR_PWM_TIM, MOTOR_PWM_APB1_PERIOD_COUNTS);
	for (index = 0U; index < 4U; index++)
	{
		MotorPWM_EnableOutput(g_motorOutputs[index].timer,
							  g_motorOutputs[index].timerChannel);
	}

	MotorPWM_StartTimer(BOARD_MOTOR_LF_PWM_TIM);
	MotorPWM_StartTimer(BOARD_MOTOR_RF_PWM_TIM);
	MotorPWM_StartTimer(BOARD_MOTOR_LR_PWM_TIM);
}

void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille)
{
	const MotorPWM_Output *output;
	uint32_t compare;

	if ((channel < MOTOR_PWM_CHANNEL_MOTOR1) ||
		(channel > MOTOR_PWM_CHANNEL_MOTOR4))
	{
		return;
	}
	if (dutyPermille > MOTOR_PWM_DUTY_MAX)
	{
		dutyPermille = MOTOR_PWM_DUTY_MAX;
	}

	output = &g_motorOutputs[channel - MOTOR_PWM_CHANNEL_MOTOR1];
	/* dutyPermille=1000 时 CCR=ARR+1，得到持续高电平的满占空比。 */
	compare = (output->periodCounts * dutyPermille) / MOTOR_PWM_DUTY_MAX;
	MotorPWM_WriteCompare(output->timer, output->timerChannel, compare);
}
