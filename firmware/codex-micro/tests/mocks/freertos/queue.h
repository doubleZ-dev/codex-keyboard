#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct mock_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,uint32_t);
int xQueueReceive(QueueHandle_t,void *,uint32_t);
