#include "BoardClock.h"
#include "stm32f4xx.h"

#define CLOCK_READY_TIMEOUT (2000000UL)

static uint8_t BoardClock_WaitForSet(volatile uint32_t *reg, uint32_t mask)
{
	uint32_t timeout = CLOCK_READY_TIMEOUT;
	while (((*reg & mask) == 0U) && (timeout != 0U))
	{
		timeout--;
	}
	return (timeout != 0U) ? 1U : 0U;
}

uint8_t BoardClock_Init(void)
{
	uint32_t pllM;
	uint32_t pllSource;
	uint8_t usingHse;

	RCC->CR |= RCC_CR_HSION;
	(void)BoardClock_WaitForSet(&RCC->CR, RCC_CR_HSIRDY);
	RCC->CFGR = 0U;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
	{
	}

	RCC->CR &= ~RCC_CR_PLLON;
	while ((RCC->CR & RCC_CR_PLLRDY) != 0U)
	{
	}

	RCC->CR |= RCC_CR_HSEON;
	usingHse = BoardClock_WaitForSet(&RCC->CR, RCC_CR_HSERDY);
	if (usingHse != 0U)
	{
		pllM = 8U;
		pllSource = RCC_PLLCFGR_PLLSRC_HSE;
	}
	else
	{
		pllM = 16U;
		pllSource = RCC_PLLCFGR_PLLSRC_HSI;
	}

	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	PWR->CR |= PWR_CR_VOS;
	FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN |
				 FLASH_ACR_ICEN | FLASH_ACR_DCEN;

	RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
	RCC->PLLCFGR = pllM | (336UL << RCC_PLLCFGR_PLLN_Pos) |
				   (0UL << RCC_PLLCFGR_PLLP_Pos) | pllSource |
				   (7UL << RCC_PLLCFGR_PLLQ_Pos);
	RCC->CR |= RCC_CR_PLLON;
	(void)BoardClock_WaitForSet(&RCC->CR, RCC_CR_PLLRDY);

	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
	{
	}

	SystemCoreClock = BOARD_SYSCLK_HZ;
	return usingHse;
}
