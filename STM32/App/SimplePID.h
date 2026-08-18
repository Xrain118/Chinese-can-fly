#ifndef __SIMPLE_PID_H
#define __SIMPLE_PID_H

typedef struct
{
	float kp;
	float ki;
	float integral;
	float outputLimit;
} SimplePID;

void SimplePID_Init(SimplePID *pid, float kp, float ki, float outputLimit);
void SimplePID_Reset(SimplePID *pid);
void SimplePID_SetGains(SimplePID *pid, float kp, float ki);
void SimplePID_SetOutputLimit(SimplePID *pid, float outputLimit);
float SimplePID_Update(SimplePID *pid, float error, float dtSeconds);

#endif
