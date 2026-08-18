#ifndef UGV_TASKS_H
#define UGV_TASKS_H

#include <stdint.h>

/* 初始化任务层依赖的队列和 IMU 初始状态；创建任务前调用。 */
void UgvTasks_Init(uint8_t imuReady);
/* 静态创建控制/协议/遥测/串口任务；全部成功返回 1。 */
uint8_t UgvTasks_Start(void);

#endif
