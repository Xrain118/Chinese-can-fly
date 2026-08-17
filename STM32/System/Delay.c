#include "Delay.h"
#include "BoardClock.h"
#include "SystemTick.h"
#include "stm32f4xx.h"

void Delay_us(uint32_t microseconds)
{
	uint32_t start = DWT->CYCCNT;
	uint32_t cycles = microseconds * (BOARD_SYSCLK_HZ / 1000000UL);
	while ((uint32_t)(DWT->CYCCNT - start) < cycles)
	{
	}
}

void Delay_ms(uint32_t milliseconds)
{
	uint32_t start = SystemTick_Millis();
	while ((uint32_t)(SystemTick_Millis() - start) < milliseconds)
	{
	}
}

void Delay_s(uint32_t seconds)
{
	while (seconds-- != 0U)
	{
		Delay_ms(1000U);
	}
}
