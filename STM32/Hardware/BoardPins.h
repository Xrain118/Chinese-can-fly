#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f4xx.h"

/*
 * SkyStar STM32F407VET6 载板引脚总表。
 *
 * 这里是硬件连线的唯一代码来源：驱动文件只引用这些宏，不在各处重复写端口。
 * 车体方向约定为 P1 在左、P2 在右、USB 朝车尾；改线时优先同步本文件和 docs。
 */

/*
 * Two external TB6612 modules:
 *   - Front module: Motor1/LF on channel A, Motor2/RF on channel B.
 *   - Rear module:  Motor3/LR on channel A, Motor4/RR on channel B.
 * The four PWM outputs are PB8..PB11; the board's PE5/PE6 and PB14/PB15
 * on-board motor-driver signals are intentionally not used.
 */
#define BOARD_MOTOR_PWM_PORT         GPIOB

#define BOARD_MOTOR_LF_PWM_PIN       8U
#define BOARD_MOTOR_LF_PWM_AF        3U
#define BOARD_MOTOR_LF_PWM_TIM       TIM10
#define BOARD_MOTOR_LF_PWM_CHANNEL   1U

#define BOARD_MOTOR_RF_PWM_PIN       9U
#define BOARD_MOTOR_RF_PWM_AF        3U
#define BOARD_MOTOR_RF_PWM_TIM       TIM11
#define BOARD_MOTOR_RF_PWM_CHANNEL   1U

#define BOARD_MOTOR_LR_PWM_PIN       10U
#define BOARD_MOTOR_LR_PWM_AF        1U
#define BOARD_MOTOR_LR_PWM_TIM       TIM2
#define BOARD_MOTOR_LR_PWM_CHANNEL   3U

#define BOARD_MOTOR_RR_PWM_PIN       11U
#define BOARD_MOTOR_RR_PWM_AF        1U
#define BOARD_MOTOR_RR_PWM_TIM       TIM2
#define BOARD_MOTOR_RR_PWM_CHANNEL   4U

#define BOARD_MOTOR_LF_CTRL_PORT     GPIOC
#define BOARD_MOTOR_LF_IN1_PIN       8U
#define BOARD_MOTOR_LF_IN2_PIN       9U

#define BOARD_MOTOR_RF_CTRL_PORT     GPIOC
#define BOARD_MOTOR_RF_IN1_PIN       10U
#define BOARD_MOTOR_RF_IN2_PIN       11U

#define BOARD_MOTOR_FRONT_STBY_PORT  GPIOC
#define BOARD_MOTOR_FRONT_STBY_PIN   12U /* Fit an external 10 kOhm pull-down. */

#define BOARD_MOTOR_LR_CTRL_PORT     GPIOD
#define BOARD_MOTOR_LR_IN1_PIN       10U
#define BOARD_MOTOR_LR_IN2_PIN       11U

#define BOARD_MOTOR_RR_CTRL_PORT     GPIOD
#define BOARD_MOTOR_RR_IN1_PIN       14U
#define BOARD_MOTOR_RR_IN2_PIN       15U

#define BOARD_MOTOR_REAR_STBY_PORT   GPIOE
#define BOARD_MOTOR_REAR_STBY_PIN    15U /* Fit an external 10 kOhm pull-down. */

/*
 * 编码器 AB 相引脚；Motor1..4 依次对应 LF、RF、LR、RR。
 * 四个轮子分别独占 TIM8、TIM3、TIM4、TIM1 的硬件编码器模式。
 * 前进计数正负不在这里改，统一在 Encoder.h 的 SIGN 中校准。
 */
#define BOARD_ENCODER_LF_PORT        GPIOC
#define BOARD_ENCODER_LF_A_PIN       6U
#define BOARD_ENCODER_LF_B_PIN       7U
#define BOARD_ENCODER_LF_AF          3U
#define BOARD_ENCODER_LF_TIM         TIM8

#define BOARD_ENCODER_RF_PORT        GPIOB
#define BOARD_ENCODER_RF_A_PIN       4U
#define BOARD_ENCODER_RF_B_PIN       5U
#define BOARD_ENCODER_RF_AF          2U
#define BOARD_ENCODER_RF_TIM         TIM3

#define BOARD_ENCODER_LR_PORT        GPIOD
#define BOARD_ENCODER_LR_A_PIN       12U
#define BOARD_ENCODER_LR_B_PIN       13U
#define BOARD_ENCODER_LR_AF          2U
#define BOARD_ENCODER_LR_TIM         TIM4

#define BOARD_ENCODER_RR_PORT        GPIOE
#define BOARD_ENCODER_RR_A_PIN       9U
#define BOARD_ENCODER_RR_B_PIN       11U
#define BOARD_ENCODER_RR_AF          1U
#define BOARD_ENCODER_RR_TIM         TIM1

/* 板载 U1T/U1R 引出的 USART1，供 HC-04 蓝牙透传文本协议使用。 */
#define BOARD_BT_USART               USART1
#define BOARD_BT_PORT                GPIOA
#define BOARD_BT_TX_PIN              9U
#define BOARD_BT_RX_PIN              10U
#define BOARD_BT_AF                  7U

/* USART2 PD5/PD6 保留作扩展串口，当前固件不初始化。 */
#define BOARD_DEBUG_USART            USART2
#define BOARD_DEBUG_PORT             GPIOD
#define BOARD_DEBUG_TX_PIN           5U
#define BOARD_DEBUG_RX_PIN           6U
#define BOARD_DEBUG_AF               7U

/*
 * Reserved board functions: USB PA11/PA12. PC8..PC12 are front-motor control
 * signals, so the optional TF interface must stay disabled with no card fitted.
 * PB5/PB8..PB11 and PE15 also replace their optional board functions.
 */

#define BOARD_PIN_MASK(pin)          ((uint16_t)(1UL << (pin)))

#endif
