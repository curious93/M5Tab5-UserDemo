/*
 * FreeRTOS idle/timer task memory hooks — allocate stacks from PSRAM.
 *
 * On ESP32-P4, pvPortMalloc uses MALLOC_CAP_INTERNAL which includes TCM
 * (0x30100068) and RTCRAM (0x5010809C).  Neither region is in the
 * SOC_BYTE_ACCESSIBLE range [0x4ff00000, 0x4ffc0000), so
 * xPortcheckValidStackMem() fails for those addresses.
 *
 * Fix: put stacks in PSRAM (byte-accessible via esp_psram_check_ptr_addr
 * with CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y) and keep TCBs in
 * static BSS (guaranteed internal SRAM).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "freertos_hooks";

/* Static TCBs live in BSS → internal SRAM, passes xPortCheckValidTCBMem. */
static StaticTask_t s_idle_tcb[2];
static StaticTask_t s_timer_tcb;

/* Stacks in PSRAM — allocated once, never freed. */
static StackType_t *s_idle_stack[2]  = {NULL, NULL};
static StackType_t *s_timer_stack    = NULL;
static int         s_idle_core_idx   = 0;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *pulIdleTaskStackSize)
{
    int idx = s_idle_core_idx++;
    if (idx >= 2) idx = 0;

    if (!s_idle_stack[idx]) {
        s_idle_stack[idx] = heap_caps_malloc(
            configMINIMAL_STACK_SIZE * sizeof(StackType_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "idle[%d] stack=%p (%u bytes) TCB=%p",
                 idx, s_idle_stack[idx],
                 (unsigned)(configMINIMAL_STACK_SIZE * sizeof(StackType_t)),
                 &s_idle_tcb[idx]);
    }
    configASSERT(s_idle_stack[idx] != NULL);

    *ppxIdleTaskTCBBuffer   = &s_idle_tcb[idx];
    *ppxIdleTaskStackBuffer = s_idle_stack[idx];
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

#if ( ( CONFIG_FREERTOS_SMP ) && ( configNUMBER_OF_CORES > 1 ) )
void vApplicationGetPassiveIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                          StackType_t  **ppxIdleTaskStackBuffer,
                                          uint32_t      *pulIdleTaskStackSize,
                                          BaseType_t     xPassiveIdleTaskIndex)
{
    vApplicationGetIdleTaskMemory(ppxIdleTaskTCBBuffer, ppxIdleTaskStackBuffer,
                                  pulIdleTaskStackSize);
}
#endif

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t  **ppxTimerTaskStackBuffer,
                                    uint32_t      *pulTimerTaskStackSize)
{
    if (!s_timer_stack) {
        s_timer_stack = heap_caps_malloc(
            configTIMER_TASK_STACK_DEPTH * sizeof(StackType_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "timer stack=%p (%u bytes) TCB=%p",
                 s_timer_stack,
                 (unsigned)(configTIMER_TASK_STACK_DEPTH * sizeof(StackType_t)),
                 &s_timer_tcb);
    }
    configASSERT(s_timer_stack != NULL);

    *ppxTimerTaskTCBBuffer   = &s_timer_tcb;
    *ppxTimerTaskStackBuffer = s_timer_stack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
