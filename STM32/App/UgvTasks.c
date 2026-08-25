/*
 * FreeRTOS 任务编排层。
 *
 * 这里把整车运行拆成四条固定职责：控制任务处理命令和电机，
 * 协议任务只读串口 RX，遥测任务只发快照，串口任务只写 TX。
 * 这种分工让“谁可以改车状态”非常明确，读代码时先从 ControlTask 看起。
 */
#include "UgvTasks.h"
#include "DebugProtocol.h"
#include "DriveControl.h"
#include "FreeRTOS.h"
#include "ProtocolTx.h"
#include "SystemTick.h"
#include "UgvCommandQueue.h"
#include "task.h"

#define UGV_CONTROL_PERIOD_MS   (10U)
#define UGV_TELEMETRY_PERIOD_MS (400U)
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

static void UgvTasks_ReplyCommandResult(const UgvCommand *command,
										  uint8_t accepted)
{
	/* 参数型命令统一用 ARG 表示校验失败，避免 switch 各分支维护两套回包逻辑。 */
	if (accepted != 0U)
	{
		DebugProtocol_SendOk(command->responseName);
	}
	else
	{
		DebugProtocol_SendErr(command->responseName, "ARG");
	}
}

/* ControlTask 是 DriveControl 的唯一写入者，避免多任务并发改车状态。 */
static void UgvTasks_HandleCommand(const UgvCommand *command)
{
	if (command == 0)
	{
		return;
	}
	switch (command->type)
	{
	case UGV_COMMAND_START:
		DriveControl_Start();
		DebugProtocol_SendOk(command->responseName);
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

	case UGV_COMMAND_GET_ALL:
		/* GET ALL 不进入 DriveControl，只要求协议层回发当前运行快照和配置。 */
		DebugProtocol_SendState();
		break;

	case UGV_COMMAND_MODE:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetMode((DriveControl_Mode)command->first));
		break;

	case UGV_COMMAND_WHEEL_PWM:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetWheelPwm((int16_t)command->first,
									 (int16_t)command->second));
		break;

	case UGV_COMMAND_SPEED:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetSpeed((int16_t)command->first));
		break;

	case UGV_COMMAND_ENCODER_ENABLE:
		DriveControl_SetEncoderClosed(command->enabled);
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_ENCODER_GAINS:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetEncoderGains(command->kp, command->ki));
		break;

	case UGV_COMMAND_ENCODER_FULL_SCALE:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetEncoderFullScaleCps(command->first));
		break;

	case UGV_COMMAND_ENCODER_LIMIT:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetEncoderLimit((int16_t)command->first));
		break;

	case UGV_COMMAND_ENCODER_SYNC_ENABLE:
		DriveControl_SetEncoderSyncEnabled(command->enabled);
		DebugProtocol_SendOk(command->responseName);
		break;

	case UGV_COMMAND_ENCODER_SYNC_PARAMS:
		UgvTasks_ReplyCommandResult(
			command,
			DriveControl_SetEncoderSync(command->kp,
										command->first,
										(int16_t)command->second));
		break;

	default:
		DebugProtocol_SendErr(command->responseName, "UNKNOWN");
		break;
	}
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

	for (;;)
	{
		uint32_t nowMs = SystemTick_Millis();
		uint32_t elapsedMs = nowMs - lastControlMs;

		/* 每个 10ms 周期先清空待处理命令，再更新电机输出。 */
		while (UgvCommandQueue_Receive(&command, 0U) != 0U)
		{
			UgvTasks_HandleCommand(&command);
			nowMs = SystemTick_Millis();
		}

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

	(void)argument;
	wakeTick = xTaskGetTickCount();
	for (;;)
	{
		vTaskDelayUntil(&wakeTick, pdMS_TO_TICKS(UGV_TELEMETRY_PERIOD_MS));
		/* 遥测只读快照并排队发送，不直接触碰串口寄存器。 */
		DebugProtocol_SendTelemetry();
	}
}

static void UgvTasks_SerialTask(void *argument)
{
	(void)argument;
	ProtocolTx_RunSerialTask();
}

void UgvTasks_Init(void)
{
	ProtocolTx_Init();
	UgvCommandQueue_Init();
}

uint8_t UgvTasks_Start(void)
{
	TaskHandle_t controlTask;
	TaskHandle_t protocolTask;
	TaskHandle_t telemetryTask;
	TaskHandle_t serialTask;

	/* 任务全部静态创建；任一创建失败都让 main 停在创建失败路径。 */
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
