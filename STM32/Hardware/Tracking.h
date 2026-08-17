#ifndef __TRACKING_H
#define __TRACKING_H

/**
 * @brief 八路循迹模块 GPIO 底层驱动。
 *
 * 模块通过 A2/A1/A0 选择一路传感器，再由共用 OUT 引脚输出该路数字量。
 * 地址 000～111 依次对应模块从左到右的 CH1～CH8。本驱动保留 OUT 的
 * 原始逻辑电平，高电平返回 1，低电平返回 0，不根据黑线或白线做反相。
 *
 * 引脚连接（占位，接线确定后替换为真实端口/引脚/时钟）：
 * - OUT：输入；
 * - A0：输出，地址 bit0；
 * - A1：输出，地址 bit1；
 * - A2：输出，地址 bit2。
 */

/* 循迹模块的传感器通道总数。 */
#define TRACKING_CHANNEL_COUNT (8U)

/* ------------------- 占位符：接线确定后替换为 GPIOA..G / GPIO_Pin_x ---------------- */

/* 共用 OUT，输入 */
#define TRACKING_OUT_PORT        GPIOX
#define TRACKING_OUT_PIN         PinX
#define TRACKING_OUT_RCC         RCC_APB2Periph_GPIOX

/* A0/A1/A2 地址线，输出 */
#define TRACKING_ADDRESS_PORT    GPIOX
#define TRACKING_A0_PIN          PinX
#define TRACKING_A1_PIN          PinX
#define TRACKING_A2_PIN          PinX
#define TRACKING_ADDRESS_RCC     RCC_APB2Periph_GPIOX

#define TRACKING_ADDRESS_PIN_MASK (TRACKING_A0_PIN | TRACKING_A1_PIN | TRACKING_A2_PIN)

/* 地址切换后等待约 1us，使多路选择器和 OUT 电平稳定后再采样。 */
#define TRACKING_ADDRESS_SETTLE_US (1U)

/**
 * @brief 初始化 OUT 输入和 A0/A1/A2 地址输出引脚。
 * @param 无。
 * @retval 无。
 * @note 初始化完成后地址线为 000，即默认选择最左侧 CH1。
 */
void Tracking_Init(void);

/**
 * @brief 按从左到右的顺序扫描全部八个循迹通道。
 * @param 无。
 * @retval 八路原始数字量组成的位图：bit0=CH1，bit1=CH2，…，bit7=CH8。
 */
uint8_t Tracking_ReadAll(void);

#endif
