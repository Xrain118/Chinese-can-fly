#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>

/* 默认硬件：100 kOhm / 33 kOhm 分压，按 3S 锂电低压保护计算。 */
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

/* 初始化 PC0/ADC1，不立即采样。 */
void PowerMonitor_Init(void);
/* 触发一次 ADC 转换并更新滤波电压；超时返回 0。 */
uint8_t PowerMonitor_Update(void);
/* 返回滤波后的电池电压，单位 mV。 */
uint32_t PowerMonitor_GetBatteryMv(void);
/* 返回最近一次 ADC 原始值，主要用于诊断和标定分压。 */
uint16_t PowerMonitor_GetRawAdc(void);

#endif
