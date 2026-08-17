#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"                  // Device header

/**
 * @brief 直流电机方向与速度接口（一块 TB6612，左右双路）。
 *
 * 本模块在 MotorPWM 之上加一层方向语义：一块 TB6612 的 A 通道（左侧前后两电机
 * 并联）由 PWM 通道1 + AIN1/AIN2 控制，B 通道（右侧前后两电机并联）由 PWM 通道2
 * + BIN1/BIN2 控制。速度命令为 -1000~+1000：正数正转、负数反转、0 为停止。
 *
 * 若使用 TB6612，其 STBY 引脚需在硬件上接 3.3V，本驱动不占用 STBY。
 *
 * 默认分配（占位，接线确定后替换为真实端口/引脚/时钟）：
 * - 左侧：PWM = MotorPWM_CH1（PWMA），方向 IN1/IN2 = AIN1/AIN2，见下方宏；
 * - 右侧：PWM = MotorPWM_CH2（PWMB），方向 IN1/IN2 = BIN1/BIN2，见下方宏。
 */

/* 速度命令范围：-1000~+1000。 */
#define MOTOR_SPEED_MAX (1000)

/* ------------------- 占位符：接线确定后替换为 GPIOA..G / GPIO_Pin_x ---------------- */

/* 方向引脚所在端口时钟（若分属多个端口，需一并加入）。 */
#define MOTOR_DIR_RCC     RCC_APB2Periph_GPIOX

#define MOTOR_LEFT_IN1_PORT   GPIOX
#define MOTOR_LEFT_IN1_PIN    PinX
#define MOTOR_LEFT_IN2_PORT   GPIOX
#define MOTOR_LEFT_IN2_PIN    PinX

#define MOTOR_RIGHT_IN1_PORT  GPIOX
#define MOTOR_RIGHT_IN1_PIN   PinX
#define MOTOR_RIGHT_IN2_PORT  GPIOX
#define MOTOR_RIGHT_IN2_PIN   PinX

/**
 * @brief 初始化左右方向引脚（并调用 MotorPWM_Init 初始化 PWM）。
 * @param 无。
 * @retval 无。
 * @note 初始化完成后两路电机 PWM 均为 0%。
 */
void Motor_Init(void);

/**
 * @brief 同时设置左右两路电机速度。
 * @param leftSpeed 左侧（A 通道）有符号速度命令，-1000~+1000。
 * @param rightSpeed 右侧（B 通道）有符号速度命令，-1000~+1000。
 * @retval 无。
 */
void Motor_SetSpeeds(int16_t leftSpeed, int16_t rightSpeed);

#endif