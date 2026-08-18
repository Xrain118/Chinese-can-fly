#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "BoardClock.h"
#include <stdint.h>

extern void vAssertCalled(const char *file, uint32_t line);

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      (BOARD_SYSCLK_HZ)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_MINI_LIST_ITEM                1
#define configSTACK_DEPTH_TYPE                  uint16_t

/* 本工程不启用 heap，所有任务/队列都必须显式提供静态控制块和栈。 */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configTOTAL_HEAP_SIZE                   0

/* 调试期尽早暴露栈越界和断言失败；车端现场会停在故障现场。 */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1

/* 暂不使用 FreeRTOS 软件定时器，周期任务全部用 vTaskDelayUntil。 */
#define configUSE_TIMERS                        0

#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_xTaskGetCurrentTaskHandle       0
#define INCLUDE_xTaskGetIdleTaskHandle          0

/* Cortex-M 异常入口直接映射到 FreeRTOS port，stm32f4xx_it.c 不再定义空壳。 */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#define configASSERT(x)                         if ((x) == 0) { vAssertCalled(__FILE__, (uint32_t)__LINE__); }

#endif
