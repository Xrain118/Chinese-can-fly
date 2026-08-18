/*
 * 电池电压采样。
 *
 * PC0 读取分压后的电池电压，本层把 ADC 原始值还原成整包电池 mV。
 * Safety 只消费滤波后的电压和 ADC 是否成功，不直接碰 ADC 寄存器。
 */
#include "PowerMonitor.h"
#include "BoardPins.h"

#define POWER_ADC_TIMEOUT_LOOPS (100000UL)
#define POWER_ADC_FILTER_WEIGHT (7UL)

static uint16_t g_rawAdc;
static uint32_t g_batteryMv;
static uint8_t g_filterReady;

void PowerMonitor_Init(void)
{
	uint32_t shift = BOARD_BATTERY_ADC_PIN * 2U;
	uint32_t sampleShift = (BOARD_BATTERY_ADC_CHANNEL - 10U) * 3U;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
	(void)RCC->APB2ENR;

	BOARD_BATTERY_ADC_PORT->MODER =
		(BOARD_BATTERY_ADC_PORT->MODER & ~(3UL << shift)) | (3UL << shift);
	BOARD_BATTERY_ADC_PORT->PUPDR &= ~(3UL << shift);

	ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
	BOARD_BATTERY_ADC->CR1 = 0U;
	BOARD_BATTERY_ADC->CR2 = 0U;
	BOARD_BATTERY_ADC->SQR1 = 0U;
	BOARD_BATTERY_ADC->SQR3 = BOARD_BATTERY_ADC_CHANNEL;
	BOARD_BATTERY_ADC->SMPR1 =
		(BOARD_BATTERY_ADC->SMPR1 & ~(7UL << sampleShift)) | (6UL << sampleShift);
	BOARD_BATTERY_ADC->CR2 = ADC_CR2_ADON;

	g_rawAdc = 0U;
	g_batteryMv = 0U;
	g_filterReady = 0U;
}

uint8_t PowerMonitor_Update(void)
{
	uint32_t timeout = POWER_ADC_TIMEOUT_LOOPS;
	uint32_t pinMv;
	uint32_t batteryMv;

	BOARD_BATTERY_ADC->SR = 0U;
	BOARD_BATTERY_ADC->CR2 |= ADC_CR2_SWSTART;
	while (((BOARD_BATTERY_ADC->SR & ADC_SR_EOC) == 0U) && (timeout != 0U))
	{
		timeout--;
	}
	if (timeout == 0U)
	{
		return 0U;
	}

	g_rawAdc = (uint16_t)BOARD_BATTERY_ADC->DR;
	/* 先算 ADC 引脚电压，再按分压电阻比例还原电池端电压。 */
	pinMv = ((uint32_t)g_rawAdc * POWER_BATTERY_ADC_REFERENCE_MV + 2047UL) / 4095UL;
	batteryMv = (uint32_t)(((uint64_t)pinMv *
		(POWER_BATTERY_DIVIDER_TOP_OHMS + POWER_BATTERY_DIVIDER_BOTTOM_OHMS)) /
		POWER_BATTERY_DIVIDER_BOTTOM_OHMS);

	if (g_filterReady == 0U)
	{
		g_batteryMv = batteryMv;
		g_filterReady = 1U;
	}
	else
	{
		/* 简单一阶 IIR 滤波，降低电机开关噪声造成的低压误判。 */
		g_batteryMv =
			(g_batteryMv * POWER_ADC_FILTER_WEIGHT + batteryMv) /
			(POWER_ADC_FILTER_WEIGHT + 1UL);
	}
	return 1U;
}

uint32_t PowerMonitor_GetBatteryMv(void)
{
	return g_batteryMv;
}

uint16_t PowerMonitor_GetRawAdc(void)
{
	return g_rawAdc;
}
