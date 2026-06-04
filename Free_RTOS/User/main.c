#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"
#include "FreeRTOS.h"
#include "task.h"

/* LED: GPIO0=D1(red), GPIO1=D2(green) on Puzhong F28335 board */
#define LED_RED   0
#define LED_GREEN 1

static void vLEDTask(void *pvParameters);

/* Static memory for Idle task */
static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName)
{
    /* Task pcTaskName overflowed its stack - halt for debug inspection */
    asm(" ESTOP0"); /* break into debugger */
    for(;;);
}

void main(void)
{
    /* Step 1: System initialization */
    InitSysCtrl();       /* PLL → 150MHz, disable watchdog */
    InitPieCtrl();       /* Init PIE control registers */
    InitPieVectTable();  /* Load default PIE vector table */

    /* Step 2: Configure LED GPIOs as outputs */
    EALLOW;
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO0  = 1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO1  = 1;
    EDIS;

    /* Step 3: Create tasks */
    xTaskCreate(vLEDTask, "LED_Red",  256, (void *)LED_RED,  1, NULL);
    xTaskCreate(vLEDTask, "LED_Green", 256, (void *)LED_GREEN, 1, NULL);

    /* Step 4: Start the scheduler — never returns */
    vTaskStartScheduler();

    /* Should never reach here */
    while (1);
}

static void vLEDTask(void *pvParameters)
{
    uint32_t led = (uint32_t)pvParameters;
    TickType_t xDelay = (led == LED_RED) ? pdMS_TO_TICKS(500) : pdMS_TO_TICKS(300);

    for (;;)
    {
        if (led == LED_RED)
            GpioDataRegs.GPATOGGLE.bit.GPIO0 = 1;   /* Toggle D1 */
        else
            GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;   /* Toggle D2 */

        vTaskDelay(xDelay);
    }
}
