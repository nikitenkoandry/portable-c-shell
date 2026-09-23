#ifndef TEST_FAKE_STREAM_BUFFER_H
#define TEST_FAKE_STREAM_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t trigger_level;
    size_t head;
    size_t tail;
    size_t count;
} StaticStreamBuffer_t;

typedef StaticStreamBuffer_t *StreamBufferHandle_t;

typedef void (*fake_stream_buffer_wait_hook_t)(StreamBufferHandle_t stream,
                                               TickType_t wait_ticks,
                                               void *ctx);

StreamBufferHandle_t xStreamBufferCreateStatic(
    size_t buffer_size_bytes,
    size_t trigger_level_bytes,
    uint8_t *storage_area,
    StaticStreamBuffer_t *stream_buffer);

size_t xStreamBufferSend(StreamBufferHandle_t stream,
                         const void *data,
                         size_t data_length_bytes,
                         TickType_t wait_ticks);

size_t xStreamBufferSendFromISR(StreamBufferHandle_t stream,
                                const void *data,
                                size_t data_length_bytes,
                                BaseType_t *higher_priority_task_woken);

size_t xStreamBufferReceive(StreamBufferHandle_t stream,
                            void *data,
                            size_t buffer_length_bytes,
                            TickType_t wait_ticks);

void fake_stream_buffer_set_wait_hook(fake_stream_buffer_wait_hook_t hook,
                                      void *ctx);

#endif
