#include "stm32f10x.h"                  // Device header
#include "Tracking.h"
#include "Delay.h"

/**
 * @brief 把三位地址编码写入 A0/A1/A2，并等待 OUT 稳定。
 * @param address 地址编码，使用低三位，范围为 0～7。
 * @retval 无。
 */
static void Tracking_SelectAddress(uint8_t address)
{
	uint16_t highPins = 0;

	/* 第一步：将地址 bit0、bit1、bit2 分别映射到 A0、A1、A2。 */
	if ((address & 0x01U) != 0U)
	{
		highPins |= TRACKING_A0_PIN;
	}
	if ((address & 0x02U) != 0U)
	{
		highPins |= TRACKING_A1_PIN;
	}
	if ((address & 0x04U) != 0U)
	{
		highPins |= TRACKING_A2_PIN;
	}

	/* 第二步：先清除旧地址，再置位新地址中需要为高的引脚。 */
	GPIO_ResetBits(TRACKING_ADDRESS_PORT, TRACKING_ADDRESS_PIN_MASK);
	GPIO_SetBits(TRACKING_ADDRESS_PORT, highPins);

	/* 第三步：等待多路选择器切换完成，避免读到前一个通道的瞬态电平。 */
	Delay_us(TRACKING_ADDRESS_SETTLE_US);
}

/**
 * @brief 初始化循迹模块使用的四个 GPIO 引脚。
 * @param 无。
 * @retval 无。
 */
void Tracking_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 使能 OUT 与地址线所在端口的时钟（同端口时重复使能无害）。 */
	RCC_APB2PeriphClockCmd(TRACKING_OUT_RCC, ENABLE);
	RCC_APB2PeriphClockCmd(TRACKING_ADDRESS_RCC, ENABLE);

	/* OUT 为输入。若循迹模块是开漏输出，需把浮空改为上拉 GPIO_Mode_IPU。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = TRACKING_OUT_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(TRACKING_OUT_PORT, &GPIO_InitStructure);

	/* A0/A1/A2 为推挽输出。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = TRACKING_ADDRESS_PIN_MASK;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(TRACKING_ADDRESS_PORT, &GPIO_InitStructure);

	/* 从地址 000/CH1 开始。 */
	GPIO_ResetBits(TRACKING_ADDRESS_PORT, TRACKING_ADDRESS_PIN_MASK);
	Delay_us(TRACKING_ADDRESS_SETTLE_US);
}

/**
 * @brief 选择并读取一个循迹通道的原始数字量。
 * @param channel 通道编号，范围为 1～8。
 * @retval 1 OUT 为高电平。
 * @retval 0 OUT 为低电平，或通道编号无效。
 */
static uint8_t Tracking_ReadChannel(uint8_t channel)
{
	/* 第一步：拒绝 1～8 以外的编号，防止无效编号被截断成其他地址。 */
	if ((channel < 1U) || (channel > TRACKING_CHANNEL_COUNT))
	{
		return 0U;
	}

	/* 第二步：CH1～CH8 转换为地址 000～111，并写入 A0/A1/A2。 */
	Tracking_SelectAddress((uint8_t)(channel - 1U));

	/* 第三步：读取共用 OUT，并把原始电平规范化为 0 或 1。 */
	return (GPIO_ReadInputDataBit(TRACKING_OUT_PORT, TRACKING_OUT_PIN) == Bit_SET) ? 1U : 0U;
}

/**
 * @brief 按从左到右顺序扫描八路循迹数字量。
 * @param 无。
 * @retval bit0～bit7 依次表示 CH1～CH8 的原始电平。
 */
uint8_t Tracking_ReadAll(void)
{
	uint8_t channel;
	uint8_t states = 0U;

	/* 依次选择 CH1～CH8；某一路为高时，将对应结果位置 1。 */
	for (channel = 1U; channel <= TRACKING_CHANNEL_COUNT; channel++)
	{
		if (Tracking_ReadChannel(channel) != 0U)
		{
			states |= (uint8_t)(1U << (channel - 1U));
		}
	}

	return states;
}
