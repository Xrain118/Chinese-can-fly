#ifndef SYSTEM_TICK_H
#define SYSTEM_TICK_H

#include <stdint.h>

/* 初始化 TIM6 毫秒节拍和 DWT 微秒计数器。 */
void SystemTick_Init(void);
/* 返回上电后的毫秒计数，使用无符号差值处理回绕。 */
uint32_t SystemTick_Millis(void);

#endif
