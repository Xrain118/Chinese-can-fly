#include "Tracking.h"
#include "BoardPins.h"
#include "Delay.h"

#define TRACKING_ADDRESS_MASK (BOARD_PIN_MASK(BOARD_TRACKING_A0_PIN) | \
								BOARD_PIN_MASK(BOARD_TRACKING_A1_PIN) | \
								BOARD_PIN_MASK(BOARD_TRACKING_A2_PIN))

static void Tracking_SelectAddress(uint8_t address)
{
	uint16_t highPins = 0U;
	if ((address & 0x01U) != 0U) highPins |= BOARD_PIN_MASK(BOARD_TRACKING_A0_PIN);
	if ((address & 0x02U) != 0U) highPins |= BOARD_PIN_MASK(BOARD_TRACKING_A1_PIN);
	if ((address & 0x04U) != 0U) highPins |= BOARD_PIN_MASK(BOARD_TRACKING_A2_PIN);

	BOARD_TRACKING_PORT->BSRR = (uint32_t)TRACKING_ADDRESS_MASK << 16U;
	BOARD_TRACKING_PORT->BSRR = highPins;
	Delay_us(TRACKING_ADDRESS_SETTLE_US);
}

void Tracking_Init(void)
{
	uint8_t pin;
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
	(void)RCC->AHB1ENR;

	for (pin = BOARD_TRACKING_A0_PIN; pin <= BOARD_TRACKING_A2_PIN; pin++)
	{
		BOARD_TRACKING_PORT->MODER =
			(BOARD_TRACKING_PORT->MODER & ~(3UL << (pin * 2U))) |
			(1UL << (pin * 2U));
		BOARD_TRACKING_PORT->OTYPER &= ~BOARD_PIN_MASK(pin);
		BOARD_TRACKING_PORT->PUPDR &= ~(3UL << (pin * 2U));
	}

	BOARD_TRACKING_PORT->MODER &= ~(3UL << (BOARD_TRACKING_OUT_PIN * 2U));
	BOARD_TRACKING_PORT->PUPDR &= ~(3UL << (BOARD_TRACKING_OUT_PIN * 2U));
	BOARD_TRACKING_PORT->BSRR = (uint32_t)TRACKING_ADDRESS_MASK << 16U;
	Delay_us(TRACKING_ADDRESS_SETTLE_US);
}

uint8_t Tracking_ReadAll(void)
{
	uint8_t channel;
	uint8_t states = 0U;
	for (channel = 0U; channel < TRACKING_CHANNEL_COUNT; channel++)
	{
		Tracking_SelectAddress(channel);
		if ((BOARD_TRACKING_PORT->IDR & BOARD_PIN_MASK(BOARD_TRACKING_OUT_PIN)) != 0U)
		{
			states |= (uint8_t)(1U << channel);
		}
	}
	return states;
}
