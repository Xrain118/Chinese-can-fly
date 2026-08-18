#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>

/* Default hardware: 100 kOhm / 33 kOhm divider and a 3S lithium battery. */
#define POWER_BATTERY_DIVIDER_TOP_OHMS       (100000UL)
#define POWER_BATTERY_DIVIDER_BOTTOM_OHMS    (33000UL)
#define POWER_BATTERY_ADC_REFERENCE_MV       (3300UL)
#define POWER_BATTERY_CELL_COUNT             (3UL)
#define POWER_BATTERY_LOW_MV_PER_CELL        (3200UL)
#define POWER_BATTERY_RECOVER_MV_PER_CELL    (3400UL)

#define POWER_BATTERY_LOW_MV \
	(POWER_BATTERY_CELL_COUNT * POWER_BATTERY_LOW_MV_PER_CELL)
#define POWER_BATTERY_RECOVER_MV \
	(POWER_BATTERY_CELL_COUNT * POWER_BATTERY_RECOVER_MV_PER_CELL)

void PowerMonitor_Init(void);
uint8_t PowerMonitor_Update(void);
uint32_t PowerMonitor_GetBatteryMv(void);
uint16_t PowerMonitor_GetRawAdc(void);

#endif
