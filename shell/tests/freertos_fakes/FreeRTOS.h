#ifndef TEST_FAKE_FREERTOS_H
#define TEST_FAKE_FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdFAIL ((BaseType_t)0)
#define pdPASS ((BaseType_t)1)
#define portMAX_DELAY ((TickType_t)UINT32_MAX)

void fake_freertos_reset(void);
unsigned int fake_freertos_critical_depth(void);
unsigned int fake_freertos_critical_errors(void);

#endif
