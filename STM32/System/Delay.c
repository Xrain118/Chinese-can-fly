/*
 * 启动阶段阻塞延时。
 *
 * 主要给外设复位、寄存器生效等硬件时序使用。调度器启动后的周期任务
 * 应优先使用 FreeRTOS 延时，不要靠这里忙等。
 */
#include "Delay.h"
#include "BoardClock.h"
#include "SystemTick.h"
#include "stm32f4xx.h"

void Delay_us(uint32_t microseconds)
{
	/* DWT CYCCNT 按 CPU 主频计数，适合 SPI/芯片复位这类短等待。 */
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
