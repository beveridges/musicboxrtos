/*
 * Minimal FreeRTOSConfig.h for RP2040 (freertos_skeleton).
 * RP2040-specific options are in the port's rp2040_config.h (included by the build).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ                         (125000000UL)
#define configTICK_RATE_HZ                          (1000)
#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configMAX_PRIORITIES                        8
#define configMINIMAL_STACK_SIZE                    (128)
#define configMAX_TASK_NAME_LEN                     16
#define configUSE_16_BIT_TICKS                      0
#define configIDLE_SHOULD_YIELD                     1
#define configUSE_TASK_NOTIFICATIONS                1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES       1
#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               0
#define configUSE_ALTERNATIVE_API                   0
#define configQUEUE_REGISTRY_SIZE                   0
#define configUSE_QUEUE_SETS                        0
#define configUSE_NEWLIB_REENTRANT                  0
#define configENABLE_BACKWARD_COMPATIBILITY         0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS     0
#define configUSE_MINI_LIST_ITEM                    1
#define configSTACK_DEPTH_TYPE                      size_t
#define configMESSAGE_BUFFER_LENGTH_TYPE            size_t

#define configUSE_TIMERS                            1
#define configTIMER_TASK_PRIORITY                   (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                    10
#define configTIMER_TASK_STACK_DEPTH                configMINIMAL_STACK_SIZE
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_vTaskDelete                         1  /* startup_task deletes itself after 1.5s */
#define INCLUDE_xTimerPendFunctionCall              1  /* required by RP2040 port for xEventGroupSetBitsFromISR */

#define configUSE_EVENT_GROUPS                      1
#define configUSE_STREAM_BUFFERS                    0

#define configSUPPORT_STATIC_ALLOCATION             1
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configKERNEL_PROVIDED_STATIC_MEMORY        1  /* use kernel-provided idle/timer task memory */
#define configTOTAL_HEAP_SIZE                       (32 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP            0
#define configENABLE_HEAP_PROTECTOR                 0

#define configKERNEL_INTERRUPT_PRIORITY             0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY        0
#define configMAX_API_CALL_INTERRUPT_PRIORITY       0

#define configUSE_IDLE_HOOK                         0
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                0
#define configUSE_DAEMON_TASK_STARTUP_HOOK          0
#define configUSE_SB_COMPLETED_CALLBACK              0
#define configCHECK_FOR_STACK_OVERFLOW              0

#define configGENERATE_RUN_TIME_STATS               0
#define configUSE_TRACE_FACILITY                    0
#define configUSE_STATS_FORMATTING_FUNCTIONS        0
#define configUSE_CO_ROUTINES                       0

/* SMP (RP2040 dual core); required by pico_flash when using FreeRTOS SMP */
#define configNUMBER_OF_CORES                       2
#define configUSE_PASSIVE_IDLE_HOOK                 0
#define configUSE_CORE_AFFINITY                     1

#endif /* FREERTOS_CONFIG_H */
