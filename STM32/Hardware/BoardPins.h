#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f4xx.h"

/*
 * SkyStar STM32F407VET6 carrier board pin map.
 * Vehicle orientation: P1 on the left, P2 on the right, USB toward the rear.
 */

/* TB6612: all control signals are grouped on GPIOE / P2. */
#define BOARD_MOTOR_PORT             GPIOE
#define BOARD_MOTOR_PWMA_PIN         5U
#define BOARD_MOTOR_PWMB_PIN         6U
#define BOARD_MOTOR_AIN1_PIN         3U
#define BOARD_MOTOR_AIN2_PIN         2U
#define BOARD_MOTOR_BIN1_PIN         1U
#define BOARD_MOTOR_BIN2_PIN         0U
#define BOARD_MOTOR_STBY_PIN         4U  /* Fit an external 10 kOhm pull-down. */
#define BOARD_MOTOR_PWM_AF           3U

/* Encoder AB pairs. Forward count polarity is adjusted in Encoder.h. */
#define BOARD_ENCODER_LF_PORT        GPIOE
#define BOARD_ENCODER_LF_A_PIN       9U
#define BOARD_ENCODER_LF_B_PIN       11U
#define BOARD_ENCODER_LF_AF          1U
#define BOARD_ENCODER_LF_TIM         TIM1

#define BOARD_ENCODER_LR_PORT        GPIOD
#define BOARD_ENCODER_LR_A_PIN       12U
#define BOARD_ENCODER_LR_B_PIN       13U
#define BOARD_ENCODER_LR_AF          2U
#define BOARD_ENCODER_LR_TIM         TIM4

#define BOARD_ENCODER_RF_PORT        GPIOB
#define BOARD_ENCODER_RF_A_PIN       4U
#define BOARD_ENCODER_RF_B_PIN       5U
#define BOARD_ENCODER_RF_AF          2U
#define BOARD_ENCODER_RF_TIM         TIM3

#define BOARD_ENCODER_RR_PORT        GPIOC
#define BOARD_ENCODER_RR_A_PIN       6U
#define BOARD_ENCODER_RR_B_PIN       7U
#define BOARD_ENCODER_RR_AF          3U
#define BOARD_ENCODER_RR_TIM         TIM8

/* Eight-channel tracking multiplexer, CH1 through CH8 from left to right. */
#define BOARD_TRACKING_PORT          GPIOE
#define BOARD_TRACKING_A0_PIN        12U
#define BOARD_TRACKING_A1_PIN        13U
#define BOARD_TRACKING_A2_PIN        14U
#define BOARD_TRACKING_OUT_PIN       15U

/* ICM-42688-P on SPI2. */
#define BOARD_IMU_SPI                SPI2
#define BOARD_IMU_SPI_PORT           GPIOB
#define BOARD_IMU_SCK_PIN            13U
#define BOARD_IMU_MISO_PIN           14U
#define BOARD_IMU_MOSI_PIN           15U
#define BOARD_IMU_SPI_AF             5U
#define BOARD_IMU_CS_PORT            GPIOD
#define BOARD_IMU_CS_PIN             8U
#define BOARD_IMU_INT_PORT           GPIOD
#define BOARD_IMU_INT_PIN            9U

/* Bluetooth tuning link on USART2. */
#define BOARD_BT_USART               USART2
#define BOARD_BT_PORT                GPIOD
#define BOARD_BT_TX_PIN              5U
#define BOARD_BT_RX_PIN              6U
#define BOARD_BT_AF                  7U

/* USART1 debug header is reserved and is not initialized by this firmware. */
#define BOARD_DEBUG_USART            USART1
#define BOARD_DEBUG_PORT             GPIOA
#define BOARD_DEBUG_TX_PIN           9U
#define BOARD_DEBUG_RX_PIN           10U
#define BOARD_DEBUG_AF               7U

/* Reserved board functions: USB PA11/PA12, TF PC8-PC12/PD2/PD3, Flash PA4-PA7. */

#define BOARD_PIN_MASK(pin)          ((uint16_t)(1UL << (pin)))

#endif
