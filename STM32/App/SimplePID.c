/*
 * 极简 PI 控制器。
 *
 * DriveControl 左右速度环各持有一个实例。这里不做参数范围校验，范围由
 * DriveControl_SetEncoderGains/Limit 负责；本层只保证积分和输出不会越过限幅。
 */
#include "SimplePID.h"

static float SimplePID_Abs(float value)
{
	return (value < 0.0f) ? -value : value;
}

static float SimplePID_Clamp(float value, float limit)
{
	if (limit < 0.0f)
	{
		limit = -limit;
	}
	if (value > limit)
	{
		return limit;
	}
	if (value < -limit)
	{
		return -limit;
	}
	return value;
}

void SimplePID_Init(SimplePID *pid, float kp, float ki, float outputLimit)
{
	if (pid == 0)
	{
		return;
	}
	pid->kp = kp;
	pid->ki = ki;
	pid->outputLimit = SimplePID_Abs(outputLimit);
	SimplePID_Reset(pid);
}

void SimplePID_Reset(SimplePID *pid)
{
	if (pid == 0)
	{
		return;
	}
	pid->integral = 0.0f;
}

void SimplePID_SetGains(SimplePID *pid, float kp, float ki)
{
	if (pid == 0)
	{
		return;
	}
	pid->kp = kp;
	pid->ki = ki;
	SimplePID_Reset(pid);
}

void SimplePID_SetOutputLimit(SimplePID *pid, float outputLimit)
{
	if (pid == 0)
	{
		return;
	}
	pid->outputLimit = SimplePID_Abs(outputLimit);
	pid->integral = SimplePID_Clamp(pid->integral, pid->outputLimit);
}

float SimplePID_Update(SimplePID *pid, float error, float dtSeconds)
{
	float output;

	if (pid == 0)
	{
		return 0.0f;
	}
	if (dtSeconds <= 0.0f)
	{
		dtSeconds = 0.001f;
	}

	/* 积分先限幅再参与输出，避免长时间误差积累后产生明显 windup。 */
	pid->integral += error * dtSeconds;
	pid->integral = SimplePID_Clamp(pid->integral, pid->outputLimit);

	output = pid->kp * error + pid->ki * pid->integral;
	output = SimplePID_Clamp(output, pid->outputLimit);

	return output;
}
