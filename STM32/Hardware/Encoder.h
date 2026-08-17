#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/* Change a sign after assembly if that wheel counts backward while moving forward. */
#define ENCODER_LEFT_FRONT_SIGN   (1)
#define ENCODER_LEFT_REAR_SIGN    (1)
#define ENCODER_RIGHT_FRONT_SIGN  (1)
#define ENCODER_RIGHT_REAR_SIGN   (1)

void Encoder_Init(void);
int32_t Encoder_GetLeftFrontCount(void);
int32_t Encoder_GetLeftRearCount(void);
int32_t Encoder_GetRightFrontCount(void);
int32_t Encoder_GetRightRearCount(void);

#endif
