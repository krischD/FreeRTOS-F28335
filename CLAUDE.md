# CLAUDE.md

本文件为 Claude Code 提供仓库上下文指引。

## 概述

FreeRTOS V10.6.2 LTS 手工移植到 TMS320F28335（C2000 32-bit DSP，FPU32，150MHz）。CCS 12.6.0 工程，TI CGT 22.6.1.LTS 编译器，**COFF 格式**（非 EABI）。

## 构建

无 CLI 构建流程，全部在 CCS IDE 内完成：
1. 打开 CCS workspace：`d:\DSP\DSP28335\FreeRTOS\`
2. Project → Import CCS Projects → 浏览到 `Free_RTOS\`
3. 点击 Build（锤子图标），或右键 `Free_RTOS` → Build Project

输出文件：`Free_RTOS/Debug/Free_RTOS.out`（COFF 可执行文件，通过 JTAG 加载）。

## 架构

### 移植来源

C28x 移植层改编自 TI 仓库 [c2000ware-FreeRTOS](https://github.com/TexasInstruments/c2000ware-FreeRTOS) 的 `portable/CCS/C2000_C28x/`。原代码面向 Gen3 C2000 器件使用 driverlib。F28335 使用传统的位域结构体头文件（`DSP2833x_Device.h`），因此四个移植文件全部去除了 driverlib 依赖。

### 移植文件改动说明

| 文件 | 关键改动 |
|------|---------|
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portdefines.h` | 删除 `#include "inc/hw_ints.h"`。`PORT_INT_YIELD` 定义为 `0x0103`（PIE 第 1 组、第 3 号中断 — 在 F28335 上为保留中断）。三个衍生宏自动计算出 PIE 寄存器地址和位掩码。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portmacro.h` | 删除 `#include "cputimer.h"`。添加后备 `HWREGH()` 宏（volatile 16 位写入）。`portYIELD()` 用 `EALLOW`/`EDIS` 包裹，因为 PIE IFR 寄存器受 EALLOW 保护。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/port.c` | 包含 `DSP2833x_Device.h` 而非 driverlib 头文件。`vPortSetupSWInterrupt()` 使用 `PieVectTable.rsvd1_3` + `PieCtrlRegs.PIEIER1.bit.INTx3` + `IER \|= M_INT1`。`vPortSetupTimerInterrupt()` 使用 `CpuTimer2Regs` 直接写寄存器 + `PieVectTable.TINT2` + `IER \|= M_INT14`。`#pragma WEAK` 已改为 `__attribute__((weak))`。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portasm.asm` | PIEACK 写操作（`MOV *(0:0x0ce1), @AL`）前后添加 `EALLOW`/`EDIS`。汇编分支：`.TMS320C2800_FPU32 = 1`（F28335 有 FPU32），`__TI_EABI__` 不定义（COFF 格式）。 |

### 关键设计决策

- **Yield 中断**：PIE 第 1 组、第 3 号中断（`0x0103`）— 在 F28335 向量表中标记为 `rsvd_ISR`。`portYIELD()` 宏通过手动置位 PIEIFR1 bit 2 触发，然后自旋 10 个周期等待 CPU 识别。这是 C28x 上 ARM PendSV 的等价实现 — C28x 没有 RTOS 专用软中断指令。
- **Tick 中断**：CPU Timer 2 → INT14（非 PIE 中断）。周期 = `configCPU_CLOCK_HZ / configTICK_RATE_HZ`。
- **Tick 与 Yield 共享 `portTICK_ISR`** 作为 ISR。ISR 检查 `bYield`：若置位则为 yield 请求；若清零则先递增 tick 计数，然后（若 `bPreemptive` 置位且时间片到期）执行上下文切换。
- **临界区**：嵌套深度计数器方案（`ulCriticalNesting`）。`portENTER_CRITICAL()` 递增 → 关 INTM。`portEXIT_CRITICAL()` 递减 → 仅当计数器归零时重新使能中断。

### 栈初始化技巧

`port.c` 中的 `pxPortInitialiseStack()` 在栈上伪造一份"中断保存栈帧"：将任务函数地址写入 PCL/PCH（待 POP 到 RPC），`pvParameters` 写入 XAR4 位置。`portRESTORE_FIRST_CONTEXT` 随后沿栈向下跳过，依次 POP XAR4 → POP RPC → LRETR，"恢复"到一个从未运行过的任务中。

### 上下文切换机制（FPU32 路径）

```
portTICK_ISR 入口:
  ASP → PUSH RB, AR1H:AR0H, RPC, XT, XAR2-7, STF, R0H-7H, DP:ST1, ulCriticalNesting
  → 存 SP 到 pxCurrentTCB->pxTopOfStack
  → 检查 bYield → 若为 tick（非 yield）则调用 xTaskIncrementTick
  → 确认 PIE 组 → 清零 bYield
  → 若 bPreemptive 或 bYield：LCR _vTaskSwitchContext（更新 pxCurrentTCB）
  → 从新任务 pxCurrentTCB->pxTopOfStack 加载 SP   ← 切换发生处
  → POP ulCriticalNesting, DP:ST1, R7H-0H, STF, XAR7-2, XT, RPC, AR1H:AR0H, RB
  → NASP → IRET → 返回到新任务
```

## 内存布局

F28335 SARAM：34K × 16-bit 字。链接脚本 `28335_RAM_lnk.cmd` 已修改，将 RAML4-L7（0xC000-0xFFFF）合并为 16K 字的连续块，供 `.ebss`/`.econst` 使用，以容纳 FreeRTOS 堆（`heap_4.c` 中的 `ucHeap[]`，8K 字）。

```
PAGE 0 (代码):  RAMM0, RAML0-L3 (4×4K), ZONE7A
PAGE 1 (数据):  RAMM1 (1K, .stack/.esysmem), RAML4-L7 合并 (16K, .ebss/.econst)
```

FreeRTOSConfig.h：`configCPU_CLOCK_HZ=150000000`，`configTICK_RATE_HZ=1000`，`configTOTAL_HEAP_SIZE=8192`（8K 字）。

## 编译器特定注意事项

- **`#pragma WEAK`**：TI CGT 22.x 不接受 `WEAK` 与 `(` 之间有空格。改用 `__attribute__((weak))`，对 GCC/Clang/TI CGT 均可移植。
- **指针转整数**：C28x 中 `int` 为 16 位，指针为 32 位。指针类型转换必须用 `uint32_t`，切勿使用 `int`。
- **EALLOW 保护的 PIE 寄存器**：`PIEIFRx`、`PIEACK`、`PIEIERx` 和 `PieVectTable` 写入前均需 `EALLOW`/`EDIS`。driverlib API 内部已处理；直接寄存器访问必须自行处理。
- **`configSUPPORT_STATIC_ALLOCATION=1`** 时，需在应用中实现 `vApplicationGetIdleTaskMemory()` 和（若使用定时器）`vApplicationGetTimerTaskMemory()`。若 `configCHECK_FOR_STACK_OVERFLOW=2`，还需实现 `vApplicationStackOverflowHook()`。
- **COFF 符号命名**：C 变量在汇编中以带下划线前缀形式出现（`pxCurrentTCB` → `_pxCurrentTCB`）。EABI 不使用下划线。portasm.asm 按 COFF 编写（`__TI_EABI__` 未定义，EABI 适配块被跳过）。

## 依赖源文件

DSP2833x 库文件（`.c`/`.asm`）源于：
`d:\DSP\DSP28335\MCU Bare-metal\TI-C2000-TMS320F28335\Code\3_F28335_LED_Demo\Library\`

DSP2833x 头文件（`.h`）源于：
`d:\DSP\DSP28335\jjb\f28335_prj\include\`

使用 TI 传统的位域结构体寄存器访问模式（例如 `GpioDataRegs.GPATOGGLE.bit.GPIO0 = 1`），而非较新的 C2000Ware driverlib API。

## CCS 头文件搜索路径

```
${PROJECT_ROOT}/FreeRTOS-Kernel/include
${PROJECT_ROOT}/FreeRTOS-Kernel/portable/CCS/C2000_C28x
${PROJECT_ROOT}/Include
${PROJECT_ROOT}
```

## 相关文档

[FreeRTOS移植记录.md](FreeRTOS移植记录.md) — 完整移植日记（中文），包含所有设计决策的背景和原理。
