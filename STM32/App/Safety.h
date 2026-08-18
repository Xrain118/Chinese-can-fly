#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

/* faultFlags 的位定义；一旦置位会锁存，直到 FAULT CLEAR 且现场条件恢复。 */
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
	/* 当前锁存故障位，协议 T/S 帧中的 F 字段直接来自这里。 */
	uint32_t faultFlags;
	/* 滤波后的电池电压，单位 mV。 */
	uint32_t batteryMv;
	/* 距离最近一次有效命令/心跳的时间，单位 ms。 */
	uint32_t watchdogAgeMs;
	uint8_t emergencyStopActive;
	uint8_t imuHealthy;
} Safety_Snapshot;

/* 初始化急停 GPIO、电池 ADC 和初始 IMU 健康状态。 */
void Safety_Init(uint32_t nowMs, uint8_t imuReady);
/* 每个控制周期调用，更新输入、锁存故障，并在故障时要求停车。 */
void Safety_Update(uint32_t nowMs);
/* 成功读到 IMU 样本后调用，用于刷新 IMU 超时看门狗。 */
void Safety_NotifyImuSample(uint32_t nowMs);
/* 收到 PWM/SPEED/PING 等上位机有效通信后调用。 */
void Safety_KickCommunication(uint32_t nowMs);
/* START 前检查是否允许运行；返回 0 表示仍有活动故障或输入不满足。 */
uint8_t Safety_RequestStart(uint32_t nowMs);
/* 尝试清除锁存故障；低压、急停、IMU 等活动条件未恢复时返回 0。 */
uint8_t Safety_ClearFaults(uint32_t nowMs);
/* 读取当前故障位。 */
uint32_t Safety_GetFaultFlags(void);
/* 拷贝安全快照给协议层输出。 */
void Safety_GetSnapshot(Safety_Snapshot *snapshot);

#endif
