/*
 * init.c
 *
 *  Created on: 2026年6月9日
 *      Author: 13077
 */
#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

void PowerUpReset(void)
{
    Uint16 Temp_WD = 0;

    if((SysCtrlRegs.WDCR & 0x80) == 0 )     /* WDFLAG = 0  如果是上电复位 */
    {
        EALLOW;

        Temp_WD = SysCtrlRegs.SCSR;
        SysCtrlRegs.SCSR = Temp_WD & 0xFE ;     /* 启动看门狗复位 */
        SysCtrlRegs.WDCR = 0x00B8;

        EDIS;
    }
    else
    {
        EALLOW;

        SysCtrlRegs.WDCR = 0x00E8;          /* 关闭看门狗 */

        EDIS;
    }
}

Uint16 CPUInit(void)
{
    /* 代码必须在复位解除后，延迟 8192 个 SYSCLKOUT 时钟周期，再对 WDFLAG 位进行采样 */
    asm(" MOV AL, #200");
    asm("PowerUpDelay:");
    asm(" SUB AL, #1");
    asm(" BF PowerUpDelay, NEQ");

    /* 上电复位，F28335芯片BUG，否则可能总线挂起，CPU会起不来 */
    PowerUpReset();

    /* System initialization */
    InitSysCtrl();       /* PLL → 150MHz, disable watchdog */
    InitPieCtrl();       /* Init PIE control registers */
    InitPieVectTable();  /* Load default PIE vector table */

    /* We must also copy required user interface functions to RAM. */
    MemCopy(&RamfuncsLoadStart, &RamfuncsLoadEnd, &RamfuncsRunStart);

    /* flash初始化 */
    InitFlash();

    /*Configure LED GPIOs as outputs */
    EALLOW;
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO0  = 1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO1  = 1;
    EDIS;

    return 0;
}


