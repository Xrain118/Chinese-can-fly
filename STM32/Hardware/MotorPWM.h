#ifndef __MOTOR_PWM_H
#define __MOTOR_PWM_H

#include "stm32f10x.h"                  // Device header

/**
 * @brief 双路电机 PWM 底层驱动（对应一块 TB6612 的 PWMA/PWMB）。
 *
 * 本模块只负责 PWM 定时器和占空比，不包含电机方向及 H 桥控制逻辑：
 * 每路 PWM 对应一个定时器通道，占空比接口为 0~1000 千分数。
 *
 * 占空比 0~1000 直接作为比较值写入对应通道（ARR=1000，1:1 对应 0%~100%）：
 * 频率 = 72MHz / (MOTOR_PWM_PRESCALER+1) / (MOTOR_PWM_PERIOD+1) ≈ 1kHz。
 *
 * 默认分配（占位，接线确定后替换为真实定时器/端口/引脚）：
 * - 通道1/左侧：CH1，对应 TB6612 PWMA；
 * - 通道2/右侧：CH2，对应 TB6612 PWMB。
 */

/* 占空比千分数上限，对应 100%。 */
#define MOTOR_PWM_DUTY_MAX   (1000U)

/* ------------------- 占位符：接线确定后替换为真实定时器/端口/引脚 ---------------- */

/* PWM 定时器 */
#define MOTOR_PWM_TIM        TIMx
#define MOTOR_PWM_RCC        RCC_APB1Periph_TIMx

/* 两个 PWM 引脚所在端口 */
#define MOTOR_PWM_GPIO_RCC   RCC_APB2Periph_GPIOX
#define MOTOR_PWM_GPIO_PORT  GPIOX
#define MOTOR_PWM_CH1_PIN    PinX
#define MOTOR_PWM_CH2_PIN    PinX

/* 时基：72MHz 分频为 1MHz，ARR=1000 得到约 1kHz、1001 档占空比。 */
#define MOTOR_PWM_PRESCALER  (72U - 1U)
#define MOTOR_PWM_PERIOD     (1000U)

/**
 * @brief 初始化双路电机 PWM。
 * @param 无。
 * @retval 无。
 * @note 初始化完成后两路 PWM 均为 0% 占空比。
 */
void MotorPWM_Init(void);

/**
 * @brief 设置指定通道的 PWM 占空比。
 * @param channel 通道编号，范围 1~2。
 * @param dutyPermille 占空比千分数，0~1000；超过 1000 时自动限幅。
 * @retval 无。
 */
void MotorPWM_SetDuty(uint8_t channel, uint16_t dutyPermille);

#endif