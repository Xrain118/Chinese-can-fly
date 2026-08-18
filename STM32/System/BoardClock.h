#ifndef BOARD_CLOCK_H
#define BOARD_CLOCK_H

#include <stdint.h>

#define BOARD_SYSCLK_HZ      (168000000UL)
#define BOARD_APB1_CLOCK_HZ  (42000000UL)
#define BOARD_APB2_CLOCK_HZ  (84000000UL)
#define BOARD_APB1_TIMER_HZ  (84000000UL)
#define BOARD_APB2_TIMER_HZ  (168000000UL)

/* 初始化系统时钟；返回 1 表示使用 8 MHz HSE，返回 0 表示退回 16 MHz HSI。 */
uint8_t BoardClock_Init(void);

#endif
