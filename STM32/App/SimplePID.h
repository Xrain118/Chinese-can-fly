#ifndef __SIMPLE_PID_H
#define __SIMPLE_PID_H

typedef struct
{
	float kp;
	float ki;
	float kd;
	float integral;
	float previousError;
	unsigned char hasPreviousError;
	float outputLimit;
} SimplePID;

void SimplePID_Init(SimplePID *pid, float kp, float ki, float kd, float outputLimit);
void SimplePID_Reset(SimplePID *pid);
void SimplePID_SetGains(SimplePID *pid, float kp, float ki, float kd);
void SimplePID_SetOutputLimit(SimplePID *pid, float outputLimit);
float SimplePID_Update(SimplePID *pid, float error, float dtSeconds);

#endif
