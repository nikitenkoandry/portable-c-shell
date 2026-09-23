#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"

#include <string.h>

static unsigned int critical_depth;
static unsigned int critical_errors;
static fake_stream_buffer_wait_hook_t wait_hook;
static void *wait_hook_ctx;

void fake_freertos_reset(void)
{
    critical_depth = 0u;
    critical_errors = 0u;
    wait_hook = NULL;
    wait_hook_ctx = NULL;
}

unsigned int fake_freertos_critical_depth(void)
{
    return critical_depth;
}

unsigned int fake_freertos_critical_errors(void)
{
    return critical_errors;
}

void fake_freertos_enter_critical(void)
{
    critical_depth++;
}

void fake_freertos_exit_critical(void)
{
    if (critical_depth == 0u) {
        critical_errors++;
        return;
    }
    critical_depth--;
}

void fake_stream_buffer_set_wait_hook(fake_stream_buffer_wait_hook_t hook,
                                      void *ctx)
{
    wait_hook = hook;
    wait_hook_ctx = ctx;
}

StreamBufferHandle_t xStreamBufferCreateStatic(
    size_t buffer_size_bytes,
    size_t trigger_level_bytes,
    uint8_t *storage_area,
    StaticStreamBuffer_t *stream_buffer)
{
    if (buffer_size_bytes < 2u || trigger_level_bytes == 0u ||
        trigger_level_bytes >= buffer_size_bytes || storage_area == NULL ||
        stream_buffer == NULL) {
        return NULL;
    }

    memset(stream_buffer, 0, sizeof(*stream_buffer));
    stream_buffer->storage = storage_area;
    stream_buffer->capacity = buffer_size_bytes - 1u;
    stream_buffer->trigger_level = trigger_level_bytes;
    return stream_buffer;
}

static size_t stream_send(StreamBufferHandle_t stream,
                          const uint8_t *data,
                          size_t data_length_bytes)
{
    size_t sent = 0u;

    if (stream == NULL || (data == NULL && data_length_bytes != 0u)) {
        return 0u;
    }

    while (sent < data_length_bytes && stream->count < stream->capacity) {
        stream->storage[stream->head] = data[sent];
        stream->head = (stream->head + 1u) % stream->capacity;
        stream->count++;
        sent++;
    }
    return sent;
}

size_t xStreamBufferSend(StreamBufferHandle_t stream,
                         const void *data,
                         size_t data_length_bytes,
                         TickType_t wait_ticks)
{
    (void)wait_ticks;
    return stream_send(stream, (const uint8_t *)data, data_length_bytes);
}

size_t xStreamBufferSendFromISR(StreamBufferHandle_t stream,
                                const void *data,
                                size_t data_length_bytes,
                                BaseType_t *higher_priority_task_woken)
{
    size_t sent = stream_send(stream,
                              (const uint8_t *)data,
                              data_length_bytes);

    if (higher_priority_task_woken != NULL && sent != 0u) {
        *higher_priority_task_woken = pdTRUE;
    }
    return sent;
}

size_t xStreamBufferReceive(StreamBufferHandle_t stream,
                            void *data,
                            size_t buffer_length_bytes,
                            TickType_t wait_ticks)
{
    uint8_t *destination = (uint8_t *)data;
    size_t received = 0u;

    if (stream == NULL || (data == NULL && buffer_length_bytes != 0u)) {
        return 0u;
    }

    if (stream->count == 0u && wait_ticks != (TickType_t)0 &&
        wait_hook != NULL) {
        wait_hook(stream, wait_ticks, wait_hook_ctx);
    }

    while (received < buffer_length_bytes && stream->count != 0u) {
        destination[received] = stream->storage[stream->tail];
        stream->tail = (stream->tail + 1u) % stream->capacity;
        stream->count--;
        received++;
    }
    return received;
}
