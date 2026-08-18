/*
 * ICM42688 IMU SPI 驱动。
 *
 * 固件只读取原始加速度/角速度/温度，不在 MCU 上融合姿态角。数据就绪由 INT1
 * 外部中断置位，ControlTask 看到标志后再通过 SPI 读一帧原始样本。
 */
#include "ICM42688.h"
#include "BoardPins.h"
#include "Delay.h"

#define ICM42688_REG_DEVICE_CONFIG  (0x11U)
#define ICM42688_REG_INT_CONFIG     (0x14U)
#define ICM42688_REG_TEMP_DATA1     (0x1DU)
#define ICM42688_REG_PWR_MGMT0      (0x4EU)
#define ICM42688_REG_GYRO_CONFIG0   (0x4FU)
#define ICM42688_REG_ACCEL_CONFIG0  (0x50U)
#define ICM42688_REG_INT_CONFIG1    (0x64U)
#define ICM42688_REG_INT_SOURCE0    (0x65U)
#define ICM42688_REG_WHO_AM_I       (0x75U)

#define ICM42688_SPI_READ           (0x80U)
#define ICM42688_SOFT_RESET         (0x01U)
#define ICM42688_INT1_PUSH_PULL_HIGH (0x03U)
#define ICM42688_ODR_1KHZ           (0x06U)
#define ICM42688_ACCEL_GYRO_LOW_NOISE (0x0FU)
#define ICM42688_UI_DRDY_INT1_EN    (0x08U)

static volatile uint8_t g_dataReady;

static void ICM42688_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

static void ICM42688_Select(uint8_t selected)
{
	if (selected != 0U)
	{
		BOARD_IMU_CS_PORT->BSRR = (uint32_t)BOARD_PIN_MASK(BOARD_IMU_CS_PIN) << 16U;
	}
	else
	{
		BOARD_IMU_CS_PORT->BSRR = BOARD_PIN_MASK(BOARD_IMU_CS_PIN);
	}
}

static uint8_t ICM42688_Transfer(uint8_t value)
{
	/* SPI2 使用 8 位访问 DR，避免 16 位写入导致一次多发一个无效字节。 */
	while ((BOARD_IMU_SPI->SR & SPI_SR_TXE) == 0U)
	{
	}
	*(__IO uint8_t *)&BOARD_IMU_SPI->DR = value;
	while ((BOARD_IMU_SPI->SR & SPI_SR_RXNE) == 0U)
	{
	}
	return *(__IO uint8_t *)&BOARD_IMU_SPI->DR;
}

static void ICM42688_WriteRegister(uint8_t address, uint8_t value)
{
	ICM42688_Select(1U);
	(void)ICM42688_Transfer((uint8_t)(address & ~ICM42688_SPI_READ));
	(void)ICM42688_Transfer(value);
	while ((BOARD_IMU_SPI->SR & SPI_SR_BSY) != 0U)
	{
	}
	ICM42688_Select(0U);
}

static void ICM42688_ReadRegisters(uint8_t address, uint8_t *data, uint8_t length)
{
	uint8_t index;
	ICM42688_Select(1U);
	(void)ICM42688_Transfer(address | ICM42688_SPI_READ);
	for (index = 0U; index < length; index++)
	{
		data[index] = ICM42688_Transfer(0xFFU);
	}
	while ((BOARD_IMU_SPI->SR & SPI_SR_BSY) != 0U)
	{
	}
	ICM42688_Select(0U);
}

static int16_t ICM42688_Combine(const uint8_t *bytes)
{
	return (int16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static void ICM42688_InitHardware(void)
{
	static const uint8_t spiPins[] =
	{
		BOARD_IMU_SCK_PIN,
		BOARD_IMU_MISO_PIN,
		BOARD_IMU_MOSI_PIN
	};
	uint8_t index;
	uint32_t csShift = BOARD_IMU_CS_PIN * 2U;
	uint32_t intShift = BOARD_IMU_INT_PIN * 2U;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
	(void)RCC->APB2ENR;

	for (index = 0U; index < (sizeof(spiPins) / sizeof(spiPins[0])); index++)
	{
		uint8_t pin = spiPins[index];
		BOARD_IMU_SPI_PORT->MODER =
			(BOARD_IMU_SPI_PORT->MODER & ~(3UL << (pin * 2U))) |
			(2UL << (pin * 2U));
		BOARD_IMU_SPI_PORT->OTYPER &= ~BOARD_PIN_MASK(pin);
		BOARD_IMU_SPI_PORT->OSPEEDR |= 2UL << (pin * 2U);
		BOARD_IMU_SPI_PORT->PUPDR &= ~(3UL << (pin * 2U));
		ICM42688_SetAlternateFunction(BOARD_IMU_SPI_PORT, pin, BOARD_IMU_SPI_AF);
	}

	BOARD_IMU_CS_PORT->BSRR = BOARD_PIN_MASK(BOARD_IMU_CS_PIN);
	BOARD_IMU_CS_PORT->MODER =
		(BOARD_IMU_CS_PORT->MODER & ~(3UL << csShift)) | (1UL << csShift);
	BOARD_IMU_CS_PORT->OTYPER &= ~BOARD_PIN_MASK(BOARD_IMU_CS_PIN);
	BOARD_IMU_CS_PORT->OSPEEDR |= 2UL << csShift;
	BOARD_IMU_CS_PORT->PUPDR &= ~(3UL << csShift);

	BOARD_IMU_INT_PORT->MODER &= ~(3UL << intShift);
	BOARD_IMU_INT_PORT->PUPDR &= ~(3UL << intShift);
	SYSCFG->EXTICR[2] = (SYSCFG->EXTICR[2] & ~(0xFUL << 4U)) | (3UL << 4U);
	EXTI->IMR |= EXTI_IMR_MR9;
	EXTI->RTSR |= EXTI_RTSR_TR9;
	EXTI->FTSR &= ~EXTI_FTSR_TR9;
	EXTI->PR = EXTI_PR_PR9;
	g_dataReady = 0U;
	NVIC_SetPriority(EXTI9_5_IRQn, 1U);
	NVIC_EnableIRQ(EXTI9_5_IRQn);

	BOARD_IMU_SPI->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_0 |
						 SPI_CR1_SSM | SPI_CR1_SSI;
	BOARD_IMU_SPI->CR2 = 0U;
	BOARD_IMU_SPI->CR1 |= SPI_CR1_SPE;
}

uint8_t ICM42688_Init(void)
{
	ICM42688_InitHardware();
	Delay_ms(2U);
	if (ICM42688_ReadWhoAmI() != ICM42688_WHO_AM_I_VALUE)
	{
		return 0U;
	}

	ICM42688_WriteRegister(ICM42688_REG_DEVICE_CONFIG, ICM42688_SOFT_RESET);
	Delay_ms(2U);
	if (ICM42688_ReadWhoAmI() != ICM42688_WHO_AM_I_VALUE)
	{
		return 0U;
	}

	ICM42688_WriteRegister(ICM42688_REG_INT_CONFIG, ICM42688_INT1_PUSH_PULL_HIGH);
	/* 0x06 配置为 1 kHz ODR，量程保持 FS_SEL=0：加速度 +/-16g，陀螺 +/-2000dps。 */
	ICM42688_WriteRegister(ICM42688_REG_GYRO_CONFIG0, ICM42688_ODR_1KHZ);
	ICM42688_WriteRegister(ICM42688_REG_ACCEL_CONFIG0, ICM42688_ODR_1KHZ);
	/* Datasheet requires INT_ASYNC_RESET (INT_CONFIG1 bit 4) to be cleared. */
	ICM42688_WriteRegister(ICM42688_REG_INT_CONFIG1, 0x00U);
	ICM42688_WriteRegister(ICM42688_REG_INT_SOURCE0, ICM42688_UI_DRDY_INT1_EN);
	ICM42688_WriteRegister(ICM42688_REG_PWR_MGMT0, ICM42688_ACCEL_GYRO_LOW_NOISE);
	Delay_ms(50U);
	return 1U;
}

uint8_t ICM42688_ReadWhoAmI(void)
{
	uint8_t value;
	ICM42688_ReadRegisters(ICM42688_REG_WHO_AM_I, &value, 1U);
	return value;
}

uint8_t ICM42688_ReadSample(ICM42688_RawSample *sample)
{
	uint8_t raw[14];
	if (sample == 0) return 0U;
	/* 从温度高字节开始连续读 14 字节，保持同一时刻的六轴数据一致。 */
	ICM42688_ReadRegisters(ICM42688_REG_TEMP_DATA1, raw, sizeof(raw));
	sample->temperature = ICM42688_Combine(&raw[0]);
	sample->accelX = ICM42688_Combine(&raw[2]);
	sample->accelY = ICM42688_Combine(&raw[4]);
	sample->accelZ = ICM42688_Combine(&raw[6]);
	sample->gyroX = ICM42688_Combine(&raw[8]);
	sample->gyroY = ICM42688_Combine(&raw[10]);
	sample->gyroZ = ICM42688_Combine(&raw[12]);
	return 1U;
}

uint8_t ICM42688_DataReady(void)
{
	uint8_t ready = g_dataReady;
	g_dataReady = 0U;
	return ready;
}

void EXTI9_5_IRQHandler(void)
{
	if ((EXTI->PR & EXTI_PR_PR9) != 0U)
	{
		EXTI->PR = EXTI_PR_PR9;
		g_dataReady = 1U;
	}
}
