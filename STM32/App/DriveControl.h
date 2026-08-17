#ifndef __DRIVE_CONTROL_H
#define __DRIVE_CONTROL_H

#include <stdint.h>

#define DRIVE_CONTROL_SENSOR_COUNT (8U)
#define DRIVE_CONTROL_PWM_MAX      (1000)

#ifndef DRIVE_CONTROL_ENABLE_HARDWARE
#define DRIVE_CONTROL_ENABLE_HARDWARE (1U)
#endif

typedef enum
{
	DRIVE_MODE_TRACK = 0,
	DRIVE_MODE_STRAIGHT = 1
} DriveControl_Mode;

typedef struct
{
	uint8_t running;
	DriveControl_Mode mode;
	uint8_t encoderClosed;
	uint8_t encoderSyncEnabled;
	uint8_t encoderSyncActive;
	uint8_t sensorBits;
	uint8_t trackingState;
	int16_t speed;
	int16_t trackingError;
	int16_t trackingCorrection;
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

void DriveControl_Init(void);
void DriveControl_LoadDefaults(void);
void DriveControl_Reset(void);
void DriveControl_Start(void);
void DriveControl_Stop(void);
uint8_t DriveControl_SetMode(DriveControl_Mode mode);
uint8_t DriveControl_SetSpeed(int16_t speed);
uint8_t DriveControl_SetTrackingGains(float kp, float ki, float kd);
uint8_t DriveControl_SetTrackingLimit(int16_t limit);
uint8_t DriveControl_SetWeight(uint8_t channel, int16_t weight);
uint8_t DriveControl_SetWeights(const int16_t weights[DRIVE_CONTROL_SENSOR_COUNT]);
void DriveControl_SetEncoderClosed(uint8_t enabled);
uint8_t DriveControl_SetEncoderGains(float kp, float ki);
uint8_t DriveControl_SetEncoderFullScaleCps(int32_t fullScaleCps);
uint8_t DriveControl_SetEncoderLimit(int16_t limit);
void DriveControl_SetEncoderSyncEnabled(uint8_t enabled);
uint8_t DriveControl_SetEncoderSync(float kp, int32_t toleranceCps, int16_t limit);
void DriveControl_Update(uint16_t elapsedMs);

void DriveControl_GetSnapshot(DriveControl_Snapshot *snapshot);
void DriveControl_FormatSensorBits(char output[9]);

float DriveControl_GetTrackingKp(void);
float DriveControl_GetTrackingKi(void);
float DriveControl_GetTrackingKd(void);
int16_t DriveControl_GetTrackingLimit(void);
int16_t DriveControl_GetWeight(uint8_t channel);
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
