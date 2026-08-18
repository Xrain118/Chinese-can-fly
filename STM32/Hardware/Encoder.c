/*
 * 四路正交编码器采集。
 *
 * 每个轮子独占一个硬件定时器的 encoder mode。本文件把 16 位硬件计数
 * 延展成带方向符号的 int32 累计计数；速度换算在 DriveControl 中完成。
 */
#include "Encoder.h"
#include "BoardPins.h"

#define ENCODER_INPUT_FILTER (4UL)

typedef struct
{
	TIM_TypeDef *timer;
	uint16_t previous;
	int32_t total;
	int8_t sign;
} Encoder_Channel;

static Encoder_Channel g_leftFront;
static Encoder_Channel g_leftRear;
static Encoder_Channel g_rightFront;
static Encoder_Channel g_rightRear;

static void Encoder_SetAlternateFunction(GPIO_TypeDef *port, uint8_t pin, uint8_t af)
{
	uint32_t shift = (uint32_t)(pin & 7U) * 4U;
	port->AFR[pin >> 3U] = (port->AFR[pin >> 3U] & ~(0xFUL << shift)) |
						 ((uint32_t)af << shift);
}

static void Encoder_InitPins(GPIO_TypeDef *port, uint8_t pinA, uint8_t pinB, uint8_t af)
{
	uint32_t maskA = 3UL << (pinA * 2U);
	uint32_t maskB = 3UL << (pinB * 2U);
	uint32_t pins = BOARD_PIN_MASK(pinA) | BOARD_PIN_MASK(pinB);

	port->MODER = (port->MODER & ~(maskA | maskB)) |
				  (2UL << (pinA * 2U)) | (2UL << (pinB * 2U));
	port->OTYPER &= ~pins;
	port->OSPEEDR = (port->OSPEEDR & ~(maskA | maskB)) |
					(1UL << (pinA * 2U)) | (1UL << (pinB * 2U));
	port->PUPDR = (port->PUPDR & ~(maskA | maskB)) |
				 (1UL << (pinA * 2U)) | (1UL << (pinB * 2U));
	Encoder_SetAlternateFunction(port, pinA, af);
	Encoder_SetAlternateFunction(port, pinB, af);
}

static void Encoder_InitTimer(TIM_TypeDef *timer)
{
	/* SMS=011：定时器由 TI1/TI2 正交编码输入驱动，CNT 随轮子转动自动增减。 */
	timer->CR1 = 0U;
	timer->PSC = 0U;
	timer->ARR = 0xFFFFU;
	timer->CNT = 0U;
	timer->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0 |
					(ENCODER_INPUT_FILTER << TIM_CCMR1_IC1F_Pos) |
					(ENCODER_INPUT_FILTER << TIM_CCMR1_IC2F_Pos);
	timer->CCER = 0U;
	timer->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;
	timer->EGR = TIM_EGR_UG;
	timer->CR1 = TIM_CR1_CEN;
}

static void Encoder_Bind(Encoder_Channel *channel, TIM_TypeDef *timer, int8_t sign)
{
	channel->timer = timer;
	channel->previous = (uint16_t)timer->CNT;
	channel->total = 0;
	channel->sign = sign;
}

static int32_t Encoder_Read(Encoder_Channel *channel)
{
	uint16_t current = (uint16_t)channel->timer->CNT;
	/* int16_t 差分天然处理 16 位计数器回绕，只要单周期增量不超过半圈范围。 */
	int16_t delta = (int16_t)(current - channel->previous);
	channel->previous = current;
	channel->total += (int32_t)delta * channel->sign;
	return channel->total;
}

void Encoder_Init(void)
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
					RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM4EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_TIM8EN;
	(void)RCC->APB2ENR;

	Encoder_InitPins(BOARD_ENCODER_LF_PORT, BOARD_ENCODER_LF_A_PIN,
					 BOARD_ENCODER_LF_B_PIN, BOARD_ENCODER_LF_AF);
	Encoder_InitPins(BOARD_ENCODER_LR_PORT, BOARD_ENCODER_LR_A_PIN,
					 BOARD_ENCODER_LR_B_PIN, BOARD_ENCODER_LR_AF);
	Encoder_InitPins(BOARD_ENCODER_RF_PORT, BOARD_ENCODER_RF_A_PIN,
					 BOARD_ENCODER_RF_B_PIN, BOARD_ENCODER_RF_AF);
	Encoder_InitPins(BOARD_ENCODER_RR_PORT, BOARD_ENCODER_RR_A_PIN,
					 BOARD_ENCODER_RR_B_PIN, BOARD_ENCODER_RR_AF);

	Encoder_InitTimer(BOARD_ENCODER_LF_TIM);
	Encoder_InitTimer(BOARD_ENCODER_LR_TIM);
	Encoder_InitTimer(BOARD_ENCODER_RF_TIM);
	Encoder_InitTimer(BOARD_ENCODER_RR_TIM);

	Encoder_Bind(&g_leftFront, BOARD_ENCODER_LF_TIM, ENCODER_LEFT_FRONT_SIGN);
	Encoder_Bind(&g_leftRear, BOARD_ENCODER_LR_TIM, ENCODER_LEFT_REAR_SIGN);
	Encoder_Bind(&g_rightFront, BOARD_ENCODER_RF_TIM, ENCODER_RIGHT_FRONT_SIGN);
	Encoder_Bind(&g_rightRear, BOARD_ENCODER_RR_TIM, ENCODER_RIGHT_REAR_SIGN);
}

int32_t Encoder_GetLeftFrontCount(void) { return Encoder_Read(&g_leftFront); }
int32_t Encoder_GetLeftRearCount(void) { return Encoder_Read(&g_leftRear); }
int32_t Encoder_GetRightFrontCount(void) { return Encoder_Read(&g_rightFront); }
int32_t Encoder_GetRightRearCount(void) { return Encoder_Read(&g_rightRear); }
