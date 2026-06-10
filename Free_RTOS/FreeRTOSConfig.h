#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

//==========================================================================
// 时钟配置
//==========================================================================
#define configCPU_CLOCK_HZ                  ( ( unsigned long ) 150000000 )
#define configTICK_RATE_HZ                  ( ( TickType_t ) 1000 )

//==========================================================================
// 调度器配置
//==========================================================================
#define configUSE_PREEMPTION                1
#define configUSE_TIME_SLICING              1
#define configUSE_IDLE_HOOK                 0
#define configUSE_TICK_HOOK                 0
#define configMAX_PRIORITIES                ( 8 )
#define configMINIMAL_STACK_SIZE            ( ( unsigned short ) 256 )
#define configTOTAL_HEAP_SIZE               ( ( size_t ) ( 8 * 1024 ) )
#define configMAX_TASK_NAME_LEN             ( 16 )
#define configUSE_TRACE_FACILITY            0
#define configUSE_16_BIT_TICKS              0
#define configIDLE_SHOULD_YIELD             0
#define configCHECK_FOR_STACK_OVERFLOW      2
#define configTIMER_TASK_STACK_DEPTH        ( ( unsigned short ) 256 )

//==========================================================================
// 内存分配
//==========================================================================
#define configSUPPORT_STATIC_ALLOCATION     1
#define configSUPPORT_DYNAMIC_ALLOCATION    1

//==========================================================================
// 任务 API 开关
//==========================================================================
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskCleanUpResources       0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskDelay                  1

//==========================================================================
// 中断嵌套 — C28x 不支持此特性，设为最低优先级即可
//==========================================================================
#define configKERNEL_INTERRUPT_PRIORITY     0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* FREERTOS_CONFIG_H */
