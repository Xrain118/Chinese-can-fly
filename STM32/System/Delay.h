#ifndef __DELAY_H
#define __DELAY_H

#include <stdint.h>

/* 短阻塞延时，主要用于外设初始化时序。 */
void Delay_us(uint32_t us);
/* 毫秒级阻塞延时，依赖 SystemTick_Millis。 */
void Delay_ms(uint32_t ms);
/* 秒级阻塞延时，保留给简单启动诊断。 */
void Delay_s(uint32_t s);

#endif
