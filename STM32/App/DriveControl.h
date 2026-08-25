#ifndef __DRIVE_CONTROL_H
#define __DRIVE_CONTROL_H

#include <stdint.h>

/* DriveControl 是车体运动状态的唯一拥有者，所有命令最终都落到这里。 */
#define DRIVE_CONTROL_PWM_MAX (1000)

#ifndef DRIVE_CONTROL_ENABLE_HARDWARE
#define DRIVE_CONTROL_ENABLE_HARDWARE (1U)
#endif

typedef enum
{
	/* DIRECT：上位机直接给左右侧 PWM/MOVE，SPEED 只保存不自动分配。 */
	DRIVE_MODE_DIRECT = 0,
	/* STRAIGHT：SPEED 同时写入左右侧目标 PWM，用于直行同速测试。 */
	DRIVE_MODE_STRAIGHT = 1
} DriveControl_Mode;

/* 控制快照给遥测/安全链读取；字段按“目标 -> 实测 -> 修正 -> 输出”组织。 */
typedef struct
{
	uint8_t running;
	DriveControl_Mode mode;
	uint8_t encoderClosed;
	uint8_t encoderSyncEnabled;
	uint8_t encoderSyncActive;
	int16_t speed;
	int16_t desiredLeftPwm;
	int16_t desiredRightPwm;
	int16_t appliedLeftPwm;
	int16_t appliedRightPwm;
	int32_t leftTargetCps;
	int32_t rightTargetCps;
	int32_t leftFrontCps;
	int32_t leftRearCps;
	int32_t rightFrontCps;
	int32_t rightRearCps;
	int32_t leftMeasuredCps;
	int32_t rightMeasuredCps;
	int32_t encoderSyncError;
	int16_t encoderSyncCorrection;
} DriveControl_Snapshot;

/* 初始化电机、编码器和默认控制参数；启动调度前调用一次。 */
void DriveControl_Init(void);
/* 恢复默认参数并停车，通常由 DEFAULTS 命令触发。 */
void DriveControl_LoadDefaults(void);
/* 清空积分、目标和输出，但不改变已保存的参数。 */
void DriveControl_Reset(void);
/* 进入运行态；真正输出在 DriveControl_Update 中更新。 */
void DriveControl_Start(void);
/* 退出运行态并立即清零 PWM。 */
void DriveControl_Stop(void);
/* 设置 DIRECT/STRAIGHT；非法模式返回 0。 */
uint8_t DriveControl_SetMode(DriveControl_Mode mode);
/* 设置 SPEED，只有 STRAIGHT 模式会立刻分配到左右轮。 */
uint8_t DriveControl_SetSpeed(int16_t speed);
/* 设置左右侧 PWM/MOVE，并强制进入 DIRECT 模式。 */
uint8_t DriveControl_SetWheelPwm(int16_t leftPwm, int16_t rightPwm);
/* 开关编码器速度闭环；切换时会重置 PI 和编码器基准。 */
void DriveControl_SetEncoderClosed(uint8_t enabled);
/* 设置左右共享速度 PI 参数，范围检查失败返回 0。 */
uint8_t DriveControl_SetEncoderGains(float kp, float ki);
/* 设置 PWM=1000 对应的现场标定 CPS，用于 PWM 到目标轮速换算。 */
uint8_t DriveControl_SetEncoderFullScaleCps(int32_t fullScaleCps);
/* 设置速度 PI 的 PWM 修正限幅。 */
uint8_t DriveControl_SetEncoderLimit(int16_t limit);
/* 开关左右速度同步 P 修正。 */
void DriveControl_SetEncoderSyncEnabled(uint8_t enabled);
/* 设置同步 P、死区容差和修正限幅。 */
uint8_t DriveControl_SetEncoderSync(float kp, int32_t toleranceCps, int16_t limit);
/* 每个控制周期调用一次，读取编码器、计算闭环修正并写电机。 */
void DriveControl_Update(uint16_t elapsedMs);

/* 拷贝当前快照；允许 snapshot 为 0，此时直接返回。 */
void DriveControl_GetSnapshot(DriveControl_Snapshot *snapshot);

/* 下面 getter 供 CFG 帧输出参数，不作为控制写入口。 */
uint8_t DriveControl_GetEncoderClosed(void);
float DriveControl_GetEncoderKp(void);
float DriveControl_GetEncoderKi(void);
int32_t DriveControl_GetEncoderFullScaleCps(void);
int16_t DriveControl_GetEncoderLimit(void);
uint8_t DriveControl_GetEncoderSyncEnabled(void);
float DriveControl_GetEncoderSyncKp(void);
int32_t DriveControl_GetEncoderSyncToleranceCps(void);
int16_t DriveControl_GetEncoderSyncLimit(void);

#endif
