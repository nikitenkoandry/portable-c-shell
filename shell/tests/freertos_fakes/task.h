#ifndef TEST_FAKE_TASK_H
#define TEST_FAKE_TASK_H

#include "FreeRTOS.h"

void fake_freertos_enter_critical(void);
void fake_freertos_exit_critical(void);

#define taskENTER_CRITICAL() fake_freertos_enter_critical()
#define taskEXIT_CRITICAL() fake_freertos_exit_critical()

#endif
