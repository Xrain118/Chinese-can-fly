/*
 * 未被 FreeRTOS 或外设驱动接管的 Cortex-M4 异常入口。
 *
 * SVC/PendSV/SysTick 由 FreeRTOSConfig.h 映射到 port 层，USART/EXTI/TIM6
 * 中断分别在对应驱动文件里实现；这里保留严重 fault 的停机现场。
 */
#include "stm32f4xx_it.h"

void NMI_Handler(void) {}

void HardFault_Handler(void)
{
	while (1) {}
}

void MemManage_Handler(void)
{
	while (1) {}
}

void BusFault_Handler(void)
{
	while (1) {}
}

void UsageFault_Handler(void)
{
	while (1) {}
}

void DebugMon_Handler(void) {}
