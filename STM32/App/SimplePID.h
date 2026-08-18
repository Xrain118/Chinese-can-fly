#ifndef __SIMPLE_PID_H
#define __SIMPLE_PID_H

typedef struct
{
	/* 只实现 P + I：速度环不用 D，避免编码器量化噪声直接放大到 PWM。 */
	float kp;
	float ki;
	float integral;
	/* 输出和积分都按该限幅裁剪，单位由调用方定义。 */
	float outputLimit;
} SimplePID;

/* 初始化 PI 状态并清空积分。 */
void SimplePID_Init(SimplePID *pid, float kp, float ki, float outputLimit);
/* 清空积分，常用于停车、反向或闭环开关切换。 */
void SimplePID_Reset(SimplePID *pid);
/* 更新比例/积分参数，并清空旧积分，避免参数切换后沿用旧误差。 */
void SimplePID_SetGains(SimplePID *pid, float kp, float ki);
/* 更新输出限幅，并把已有积分裁剪到新范围内。 */
void SimplePID_SetOutputLimit(SimplePID *pid, float outputLimit);
/* 用 error 和 dt 计算本周期修正量；返回值已限幅。 */
float SimplePID_Update(SimplePID *pid, float error, float dtSeconds);

#endif
