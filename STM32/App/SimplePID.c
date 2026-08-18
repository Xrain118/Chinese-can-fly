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

	pid->integral += error * dtSeconds;
	pid->integral = SimplePID_Clamp(pid->integral, pid->outputLimit);

	output = pid->kp * error + pid->ki * pid->integral;
	output = SimplePID_Clamp(output, pid->outputLimit);

	return output;
}
