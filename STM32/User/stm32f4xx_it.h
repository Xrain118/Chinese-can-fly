#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

/* 只声明本文件实际提供的异常入口；RTOS 和外设中断入口分散在各自模块。 */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

#endif
