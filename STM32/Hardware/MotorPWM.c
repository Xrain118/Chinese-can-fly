#include "MotorPWM.h"
#include "BoardPins.h"
#include "BoardClock.h"

#define MOTOR_PWM_PERIOD_COUNTS (BOARD_APB2_TIMER_HZ / MOTOR_PWM_HZ)

static void MotorPWM_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

void MotorPWM_Init(void)
{
	uint32_t modeMask = (3UL << (BOARD_MOTOR_PWMA_PIN * 2U)) |
						(3UL << (BOARD_MOTOR_PWMB_PIN * 2U));

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
	(void)RCC->APB2ENR;

	BOARD_MOTOR_PORT->MODER = (BOARD_MOTOR_PORT->MODER & ~modeMask) |
							(2UL << (BOARD_MOTOR_PWMA_PIN * 2U)) |
							(2UL << (BOARD_MOTOR_PWMB_PIN * 2U));
	BOARD_MOTOR_PORT->OTYPER &= ~(BOARD_PIN_MASK(BOARD_MOTOR_PWMA_PIN) |
								 BOARD_PIN_MASK(BOARD_MOTOR_PWMB_PIN));
	BOARD_MOTOR_PORT->OSPEEDR |= modeMask;
	BOARD_MOTOR_PORT->PUPDR &= ~modeMask;
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PORT, BOARD_MOTOR_PWMA_PIN,
								  BOARD_MOTOR_PWM_AF);
	MotorPWM_SetAlternateFunction(BOARD_MOTOR_PORT, BOARD_MOTOR_PWMB_PIN,
								  BOARD_MOTOR_PWM_AF);

	TIM9->CR1 = 0U;
	TIM9->PSC = 0U;
	TIM9->ARR = (uint16_t)(MOTOR_PWM_PERIOD_COUNTS - 1UL);
	TIM9->CCR1 = 0U;
	TIM9->CCR2 = 0U;
	TIM9->CCMR1 = TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 |
				  TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
	TIM9->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
	TIM9->EGR = TIM_EGR_UG;
	TIM9->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
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
		TIM9->CCR1 = compare;
	}
	else if (channel == 2U)
	{
		TIM9->CCR2 = compare;
	}
}
