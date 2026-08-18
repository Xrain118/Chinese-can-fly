#ifndef UGV_COMMAND_QUEUE_H
#define UGV_COMMAND_QUEUE_H

#include "UgvCommand.h"
#include <stdint.h>

/* 创建静态命令队列；协议任务是生产者，ControlTask 是唯一消费者。 */
void UgvCommandQueue_Init(void);
/* 非阻塞投递一条已解析命令；队列满返回 0，由协议层回 ERR BUSY。 */
uint8_t UgvCommandQueue_Send(const UgvCommand *command);
/* 接收一条命令，waitMs 为等待时间；ControlTask 通常用 0 清空队列。 */
uint8_t UgvCommandQueue_Receive(UgvCommand *command, uint32_t waitMs);

#endif
