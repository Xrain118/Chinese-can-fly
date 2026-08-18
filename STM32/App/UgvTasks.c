#include "UgvTasks.h"
#include "DebugProtocol.h"
#include "DriveControl.h"
#include "FreeRTOS.h"
#include "ICM42688.h"
#include "ProtocolTx.h"
#include "Safety.h"
#include "SystemTick.h"
#include "UgvCommandQueue.h"
#include "task.h"

#define UGV_CONTROL_PERIOD_MS   (10U)
#define UGV_TELEMETRY_PERIOD_MS (50U)
#define UGV_PROTOCOL_PERIOD_MS  (1U)

#define UGV_CONTROL_STACK_WORDS   (512U)
#define UGV_PROTOCOL_STACK_WORDS  (512U)
#define UGV_TELEMETRY_STACK_WORDS (512U)
#define UGV_SERIAL_STACK_WORDS    (384U)

#define UGV_CONTROL_PRIORITY   (tskIDLE_PRIORITY + 4U)
#define UGV_PROTOCOL_PRIORITY  (tskIDLE_PRIORITY + 3U)
#define UGV_TELEMETRY_PRIORITY (tskIDLE_PRIORITY + 2U)
#define UGV_SERIAL_PRIORITY    (tskIDLE_PRIORITY + 2U)

/* 所有任务和队列均静态分配，便于在车端控制内存占用和启动失败路径。 */
static StaticTask_t g_controlTaskControl;
static StaticTask_t g_protocolTaskControl;
static StaticTask_t g_telemetryTaskControl;
static StaticTask_t g_serialTaskControl;

static StackType_t g_controlTaskStack[UGV_CONTROL_STACK_WORDS];
static StackType_t g_protocolTaskStack[UGV_PROTOCOL_STACK_WORDS];
static StackType_t g_telemetryTaskStack[UGV_TELEMETRY_STACK_WORDS];
static StackType_t g_serialTaskStack[UGV_SERIAL_STACK_WORDS];

static uint8_t g_imuReady;
static uint8_t g_imuSampleValid;
static ICM42688_RawSample g_latestImuSample;

/* ControlTask 是 DriveControl/Safety 的唯一写入者，避免多任务并发改车状态。 */
static void UgvTasks_HandleCommand(const UgvCommand *command, uint32_t nowMs)
{
	if (command == 0)
	{
		return;
	}
	switch (command->type)
	{
	case UGV_COMMAND_START:
		if (Safety_RequestStart(nowMs) != 0U)
		{
			DriveControl_Start();
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "FAULT");
		}
		break;

	case UGV_COMMAND_STOP:
		DriveControl_Stop();
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_RESET:
		DriveControl_Reset();
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_DEFAULTS:
		DriveControl_LoadDefaults();
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_PING:
		Safety_KickCommunication(nowMs);
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_GET_ALL:
		DebugProtocol_SendState();
		break;

	case UGV_COMMAND_FAULT_CLEAR:
		if (Safety_ClearFaults(nowMs) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ACTIVE");
		}
		break;

	case UGV_COMMAND_MODE:
		if (DriveControl_SetMode((DriveControl_Mode)command->first) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_WHEEL_PWM:
		if (DriveControl_SetWheelPwm((int16_t)command->first,
									 (int16_t)command->second) != 0U)
		{
			Safety_KickCommunication(nowMs);
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_SPEED:
		if (DriveControl_SetSpeed((int16_t)command->first) != 0U)
		{
			Safety_KickCommunication(nowMs);
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_ENCODER_ENABLE:
		DriveControl_SetEncoderClosed(command->enabled);
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_ENCODER_GAINS:
		if (DriveControl_SetEncoderGains(command->kp, command->ki) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_ENCODER_FULL_SCALE:
		if (DriveControl_SetEncoderFullScaleCps(command->first) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_ENCODER_LIMIT:
		if (DriveControl_SetEncoderLimit((int16_t)command->first) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	case UGV_COMMAND_ENCODER_SYNC_ENABLE:
		DriveControl_SetEncoderSyncEnabled(command->enabled);
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_ENCODER_SYNC_PARAMS:
		if (DriveControl_SetEncoderSync(command->kp,
										command->first,
										(int16_t)command->second) != 0U)
		{
			DebugProtocol_SendOk(command->responseName);
		}
		else
		{
			DebugProtocol_SendErr(command->responseName, "ARG");
		}
		break;

	default:
		DebugProtocol_SendErr(command->responseName, "UNKNOWN");
		break;
	}
}

static void UgvTasks_SaveImuSample(const ICM42688_RawSample *sample)
{
	/* TelemetryTask 会异步读取最新样本，复制结构体时用临界区保证一致性。 */
	taskENTER_CRITICAL();
	g_latestImuSample = *sample;
	g_imuSampleValid = 1U;
	taskEXIT_CRITICAL();
}

static uint8_t UgvTasks_LoadImuSample(ICM42688_RawSample *sample)
{
	uint8_t valid;

	/* 只共享最近一帧原始 IMU，不排队历史帧，避免遥测阻塞控制任务。 */
	taskENTER_CRITICAL();
	valid = g_imuSampleValid;
	if ((valid != 0U) && (sample != 0))
	{
		*sample = g_latestImuSample;
	}
	taskEXIT_CRITICAL();
	return valid;
}

static void UgvTasks_ControlTask(void *argument)
{
	TickType_t wakeTick;
	uint32_t lastControlMs;
	UgvCommand command;

	(void)argument;
	wakeTick = xTaskGetTickCount();
	lastControlMs = SystemTick_Millis();

	/* 启动诊断也走 TX 队列，保证调度后所有串口输出都由 SerialTxTask 串行化。 */
	DebugProtocol_SendState();
	if (g_imuReady != 0U)
	{
		(void)ProtocolTx_Printf("OK C=IMU,WHO=%u\r\n", ICM42688_ReadWhoAmI());
	}
	else
	{
		(void)ProtocolTx_SendString("ERR C=IMU,M=WHO_AM_I\r\n");
	}

	for (;;)
	{
		uint32_t nowMs = SystemTick_Millis();
		uint32_t elapsedMs = nowMs - lastControlMs;

		/* 每个 10ms 周期先清空待处理命令，再更新安全链和电机输出。 */
		while (UgvCommandQueue_Receive(&command, 0U) != 0U)
		{
			UgvTasks_HandleCommand(&command, nowMs);
			nowMs = SystemTick_Millis();
		}

		if ((g_imuReady != 0U) && (ICM42688_DataReady() != 0U))
		{
			ICM42688_RawSample sample;
			if (ICM42688_ReadSample(&sample) != 0U)
			{
				UgvTasks_SaveImuSample(&sample);
				Safety_NotifyImuSample(nowMs);
			}
		}

		Safety_Update(nowMs);
		if (elapsedMs > 100U)
		{
			/* 调试暂停或异常卡顿后限制 dt，防止 PI 积分一次性跳太大。 */
			elapsedMs = 100U;
		}
		if (elapsedMs == 0U)
		{
			elapsedMs = UGV_CONTROL_PERIOD_MS;
		}
		lastControlMs = nowMs;
		DriveControl_Update((uint16_t)elapsedMs);
		vTaskDelayUntil(&wakeTick, pdMS_TO_TICKS(UGV_CONTROL_PERIOD_MS));
	}
}

static void UgvTasks_ProtocolTask(void *argument)
{
	(void)argument;
	for (;;)
	{
		/* RX 仍沿用 USART 中断环形缓冲；本任务只取字节、组行、投递命令。 */
		DebugProtocol_Run();
		vTaskDelay(pdMS_TO_TICKS(UGV_PROTOCOL_PERIOD_MS));
	}
}

static void UgvTasks_TelemetryTask(void *argument)
{
	TickType_t wakeTick;
	ICM42688_RawSample sample;

	(void)argument;
	wakeTick = xTaskGetTickCount();
	for (;;)
	{
		vTaskDelayUntil(&wakeTick, pdMS_TO_TICKS(UGV_TELEMETRY_PERIOD_MS));
		/* 遥测只读快照并排队发送，不直接触碰串口寄存器。 */
		DebugProtocol_SendTelemetry();
		if (UgvTasks_LoadImuSample(&sample) != 0U)
		{
			DebugProtocol_SendImuRaw(&sample);
		}
	}
}

static void UgvTasks_SerialTask(void *argument)
{
	(void)argument;
	ProtocolTx_RunSerialTask();
}

void UgvTasks_Init(uint8_t imuReady)
{
	g_imuReady = (imuReady != 0U) ? 1U : 0U;
	g_imuSampleValid = 0U;
	ProtocolTx_Init();
	UgvCommandQueue_Init();
}

uint8_t UgvTasks_Start(void)
{
	TaskHandle_t controlTask;
	TaskHandle_t protocolTask;
	TaskHandle_t telemetryTask;
	TaskHandle_t serialTask;

	serialTask = xTaskCreateStatic(UgvTasks_SerialTask, "serial_tx",
								   UGV_SERIAL_STACK_WORDS, 0,
								   UGV_SERIAL_PRIORITY, g_serialTaskStack,
								   &g_serialTaskControl);
	controlTask = xTaskCreateStatic(UgvTasks_ControlTask, "control",
									UGV_CONTROL_STACK_WORDS, 0,
									UGV_CONTROL_PRIORITY, g_controlTaskStack,
									&g_controlTaskControl);
	protocolTask = xTaskCreateStatic(UgvTasks_ProtocolTask, "protocol_rx",
									 UGV_PROTOCOL_STACK_WORDS, 0,
									 UGV_PROTOCOL_PRIORITY, g_protocolTaskStack,
									 &g_protocolTaskControl);
	telemetryTask = xTaskCreateStatic(UgvTasks_TelemetryTask, "telemetry",
									  UGV_TELEMETRY_STACK_WORDS, 0,
									  UGV_TELEMETRY_PRIORITY,
									  g_telemetryTaskStack,
									  &g_telemetryTaskControl);
	return ((serialTask != 0) && (controlTask != 0) &&
			(protocolTask != 0) && (telemetryTask != 0)) ? 1U : 0U;
}
