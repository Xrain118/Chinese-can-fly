#ifndef BOARD_CLOCK_H
#define BOARD_CLOCK_H

#include <stdint.h>

#define BOARD_SYSCLK_HZ      (168000000UL)
#define BOARD_APB1_CLOCK_HZ  (42000000UL)
#define BOARD_APB2_CLOCK_HZ  (84000000UL)
#define BOARD_APB1_TIMER_HZ  (84000000UL)
#define BOARD_APB2_TIMER_HZ  (168000000UL)

/* Returns 1 when the 8 MHz HSE is used, 0 when the 16 MHz HSI fallback is used. */
uint8_t BoardClock_Init(void);

#endif
