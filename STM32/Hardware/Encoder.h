#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/* 现场校准点：小车向前推时某轮计数为负，就只翻转对应符号。 */
#define ENCODER_LEFT_FRONT_SIGN   (-1)
#define ENCODER_LEFT_REAR_SIGN    (-1)
#define ENCODER_RIGHT_FRONT_SIGN  (1)
#define ENCODER_RIGHT_REAR_SIGN   (1)

/* 配置四个定时器为正交编码器模式，并建立软件累计计数。 */
void Encoder_Init(void);
/* 返回带方向符号的软件累计计数；速度差分由 DriveControl 完成。 */
int32_t Encoder_GetLeftFrontCount(void);
int32_t Encoder_GetLeftRearCount(void);
int32_t Encoder_GetRightFrontCount(void);
int32_t Encoder_GetRightRearCount(void);

#endif
