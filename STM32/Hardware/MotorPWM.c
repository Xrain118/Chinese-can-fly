#include "MotorPWM.h"
#include "BoardPins.h"
#include "BoardClock.h"

#define MOTOR_PWM_PERIOD_COUNTS (BOARD_APB1_TIMER_HZ / MOTOR_PWM_HZ)

static void MotorPWM_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

void MotorPWM_Init(void)
{
	uint32_t modeMask = (3UL << (BOARD_MOTOR_LF_PWM_PIN * 2U)) |
						(3UL << (BOARD_MOTOR_LR_PWM_PIN * 2U)) |
						(3UL << (BOARD_MOTOR_RF_PWM_PIN * 2U)) |
						(3UL << (BOARD_MOTOR_RR_PWM_PIN * 2U));
	uint32_t pins = BOARD_PIN_MASK(BOARD_MOTOR_LF_PWM_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_LR_PWM_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RF_PWM_PIN) |
					BOARD_PIN_MASK(BOARD_MOTOR_RR_PWM_PIN);

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
	(void)RCC->APB1ENR;

	BOARD_MOTOR_PWM_PORT->MODER = (BOARD_MOTOR_PWM_PORT->MODER & ~modeMask) |
								  (2UL << (BOARD_MOTOR_LF_PWM_PIN * 2U)) |
								  (2UL << (BOARD_MOTOR_LR_PWM_PIN * 2U)) |
								  (2UL << (BOARD_MOTOR_RF_PWM_PIN * 2U)) |
								  (2UL << (BOARD_MOTOR_RR_PWM_PIN * 2U));
	BOARD_MOTOR_PWM_PORT->OTYPER &= ~pins;
	BOARD_MOTOR_PWM_PORT->OSPEEDR |= modeMask;
	BOARD_MOTOR_PWM_PORT->PUPDR &= ~modeMask;
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PWM_PORT, BOARD_MOTOR_LF_PWM_PIN,
								  BOARD_MOTOR_PWM_AF);
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PWM_PORT, BOARD_MOTOR_LR_PWM_PIN,
								  BOARD_MOTOR_PWM_AF);
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PWM_PORT, BOARD_MOTOR_RF_PWM_PIN,
								  BOARD_MOTOR_PWM_AF);
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PWM_PORT, BOARD_MOTOR_RR_PWM_PIN,
								  BOARD_MOTOR_PWM_AF);

	TIM5->CR1 = 0U;
	TIM5->PSC = 0U;
	TIM5->ARR = MOTOR_PWM_PERIOD_COUNTS - 1UL;
	TIM5->CCR1 = 0U;
	TIM5->CCR2 = 0U;
	TIM5->CCR3 = 0U;
	TIM5->CCR4 = 0U;
	TIM5->CCMR1 = TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 |
				  TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
	TIM5->CCMR2 = TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2 |
				  TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2;
	TIM5->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E |
				 TIM_CCER_CC3E | TIM_CCER_CC4E;
	TIM5->EGR = TIM_EGR_UG;
	TIM5->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille)
{
	uint32_t compare;
	if (dutyPermille > MOTOR_PWM_DUTY_MAX)
	{
		dutyPermille = MOTOR_PWM_DUTY_MAX;
	}
	compare = (MOTOR_PWM_PERIOD_COUNTS * dutyPermille) / MOTOR_PWM_DUTY_MAX;
	if (channel == 1U)
	{
		TIM5->CCR1 = compare;
	}
	else if (channel == 2U)
	{
		TIM5->CCR2 = compare;
	}
	else if (channel == 3U)
	{
		TIM5->CCR3 = compare;
	}
	else if (channel == 4U)
	{
		TIM5->CCR4 = compare;
	}
}
