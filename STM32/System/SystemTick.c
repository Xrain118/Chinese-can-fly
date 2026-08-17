#include "SystemTick.h"
#include "BoardClock.h"
#include "stm32f4xx.h"

static volatile uint32_t g_milliseconds;

void SystemTick_Init(void)
{
	RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
	(void)RCC->APB1ENR;

	TIM6->CR1 = 0U;
	TIM6->PSC = (uint16_t)((BOARD_APB1_TIMER_HZ / 1000000UL) - 1UL);
	TIM6->ARR = 1000U - 1U;
	TIM6->EGR = TIM_EGR_UG;
	TIM6->SR = 0U;
	TIM6->DIER = TIM_DIER_UIE;

	g_milliseconds = 0U;
	NVIC_SetPriority(TIM6_DAC_IRQn, 3U);
	NVIC_EnableIRQ(TIM6_DAC_IRQn);
	TIM6->CR1 = TIM_CR1_CEN;

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0U;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t SystemTick_Millis(void)
{
	return g_milliseconds;
}

void TIM6_DAC_IRQHandler(void)
{
	if ((TIM6->SR & TIM_SR_UIF) != 0U)
	{
		TIM6->SR &= ~TIM_SR_UIF;
		g_milliseconds++;
	}
}
