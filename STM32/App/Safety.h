#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

#define SAFETY_FAULT_COMM_TIMEOUT   (1UL << 0U)
#define SAFETY_FAULT_ESTOP          (1UL << 1U)
#define SAFETY_FAULT_LOW_BATTERY    (1UL << 2U)
#define SAFETY_FAULT_IMU            (1UL << 3U)
#define SAFETY_FAULT_ENCODER        (1UL << 4U)
#define SAFETY_FAULT_BATTERY_ADC    (1UL << 5U)

#define SAFETY_COMM_TIMEOUT_MS      (300UL)
#define SAFETY_IMU_TIMEOUT_MS       (250UL)
#define SAFETY_LOW_BATTERY_DELAY_MS (1000UL)
#define SAFETY_ENCODER_DELAY_MS     (1000UL)
#define SAFETY_ENCODER_TEST_PWM     (250)
#define SAFETY_ENCODER_MIN_CPS      (50)

typedef struct
{
	uint32_t faultFlags;
	uint32_t batteryMv;
	uint32_t watchdogAgeMs;
	uint8_t emergencyStopActive;
	uint8_t imuHealthy;
} Safety_Snapshot;

void Safety_Init(uint32_t nowMs, uint8_t imuReady);
void Safety_Update(uint32_t nowMs);
void Safety_NotifyImuSample(uint32_t nowMs);
void Safety_KickCommunication(uint32_t nowMs);
uint8_t Safety_RequestStart(uint32_t nowMs);
uint8_t Safety_ClearFaults(uint32_t nowMs);
uint32_t Safety_GetFaultFlags(void);
void Safety_GetSnapshot(Safety_Snapshot *snapshot);

#endif
