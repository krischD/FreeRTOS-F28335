# CLAUDE.md

本文件为 Claude Code 提供仓库上下文指引。

## 概述

FreeRTOS V10.6.2 LTS 手工移植到 TMS320F28335（C2000 32-bit DSP，FPU32，150MHz）。CCS 12.6.0 工程，TI CGT 22.6.1.LTS 编译器，**COFF 格式**（非 EABI）。

**Flash Boot + RAM 运行架构**：代码存储在片上 Flash，性能敏感的 ISR 和 Flash API 在启动时拷贝到 SARAM 执行。

## 构建

无 CLI 构建流程，全部在 CCS IDE 内完成：
1. 打开 CCS workspace：`d:\DSP\DSP28335\`
2. Project → Import CCS Projects → 浏览到 `Free_RTOS\`
3. 点击 Build（锤子图标），或右键 `Free_RTOS` → Build Project

输出与调试文件：`Free_RTOS/Debug/Free_RTOS.out`（COFF），`.map`（链接映射），`_linkInfo.xml`（链接详情），通过 JTAG 加载。

### 编译器关键标志（`.cproject` Debug 配置）

| 标志 | 含义 |
|------|------|
| `--silicon_version=28` | C28x 目标架构 |
| `--float_support=fpu32` | 启用 FPU32 硬件浮点 |
| `--large_memory_model` (-ml) | 大内存模型（指针 32 位） |
| `--unified_memory` (-mt) | 统一内存模型 |
| `--abi=coffabi` | COFF ABI（非 EABI） |
| `--stack_size=0x300` | 系统栈 768 字 |
| RTS 库 `libc.a` | COFF 运行时支持库 |

> **注意**：Release 配置的头文件搜索路径不完整，缺少 FreeRTOS-Kernel 和 Include 目录。若切换 Release 构建需手动在 CCS 中补全 include 路径。

## 启动流程

```
复位 → Boot ROM → codestart (0x33FFF6)
  → wd_disable (关看门狗)
  → _c_int00 (RTS 库 C 环境初始化)
  → main()
    → CPUInit() [Library/init.c]:
        PowerUpReset()       // F28335 上电复位 BUG 修复
        InitSysCtrl()        // PLL → 150MHz
        InitPieCtrl()
        InitPieVectTable()
        MemCopy(&RamfuncsLoadStart, &RamfuncsLoadEnd, &RamfuncsRunStart)  // 拷贝 ramfuncs
        InitFlash()          // Flash 初始化
        配置 GPIO0/1 为输出
    → xTaskCreate × 2
    → vTaskStartScheduler()
```

## 应用层

`User/main.c` 创建两个 LED 闪烁任务验证多任务调度：

| 任务 | GPIO | 周期 | 栈 |
|------|------|------|-----|
| LED_Red | GPIO0 | 500ms | 256 字 |
| LED_Green | GPIO1 | 300ms | 256 字 |

空闲任务使用静态内存分配（`configSUPPORT_STATIC_ALLOCATION=1`），需在应用中实现 `vApplicationGetIdleTaskMemory()`。栈溢出检测（`configCHECK_FOR_STACK_OVERFLOW=2`）触发 `ESTOP0` 断点。

## 内存布局

链接脚本：`Library/cmd/F28335_APP.cmd` + `Library/cmd/DSP2833x_Headers_nonBIOS.cmd`。

### F28335_APP.cmd 关键段

| 段 | 存储位置（Flash） | 运行位置（RAM） | 说明 |
|------|------|------|------|
| `ramfuncs` | FLASHC_0 (0x328000) | RAML2 (0x00A000) | ISR + Flash API，启动时 MemCopy 拷贝 |
| `xintffuncs` | FLASHD_H (0x300012) | ZONE7A (0x200000) | 外部 SRAM 运行 |
| `Flash28_API` | FLASHC_0 (0x328000) | RAML2 (0x00A000) | Flash API 库 |
| `.text` | FLASHD_H (0x300012) | —（Flash 直接执行） | 应用程序代码 |
| `.cinit` | FLASHD_H | — | 初始化表 |
| `.econst` | FLASHD_H | — | 常量数据 |
| `.ebss` | — | RAML4_7 (0x00C000, 16K) | 未初始化全局变量 + FreeRTOS 堆 |
| `.stack` | — | RAML1 (0x009000) | 系统栈 |
| `.esysmem` | — | RAML1 | 动态内存（malloc） |

### RAM 拷贝符号

链接器自动生成以下符号供 `MemCopy()` 使用：
- `_RamfuncsLoadStart` / `_RamfuncsLoadEnd` / `_RamfuncsRunStart`
- `_XintffuncsLoadStart` / `_XintffuncsLoadEnd` / `_XintffuncsRunStart`
- `_Flash28_API_LoadStart` / `_Flash28_API_LoadEnd` / `_Flash28_API_RunStart`

### 关键 RAM 分配

```
RAML0 (0x008000): 0x700 字 BOOT 代码拷贝目标
RAML2 (0x00A000): 0x1000 字 ramfuncs + Flash28_API 运行目标
RAML4_7 (0x00C000): 16K 字 .ebss/.econst
```

## 架构

### 移植来源

C28x 移植层改编自 TI 仓库 [c2000ware-FreeRTOS](https://github.com/TexasInstruments/c2000ware-FreeRTOS) 的 `portable/CCS/C2000_C28x/`。原代码面向 Gen3 C2000 器件使用 driverlib。F28335 使用传统的位域结构体头文件（`DSP2833x_Device.h`），因此四个移植文件全部去除了 driverlib 依赖。

### 移植文件改动说明

| 文件 | 关键改动 |
|------|---------|
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portasm.asm` | `.sect "ramfuncs"` 在文件顶部第 52 行（`.if FPU32` 之前），使所有汇编函数（两种路径）均进入 ramfuncs 段。PIEACK 写操作前后添加 `EALLOW`/`EDIS`。FPU32 路径：`.TMS320C2800_FPU32 = 1`，EABI 未定义（COFF）。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portdefines.h` | 删除 `#include "inc/hw_ints.h"`。`PORT_INT_YIELD` 定义为 `0x0103`（PIE 第 1 组、第 3 号中断 — 在 F28335 上为保留中断）。三个衍生宏自动计算出 PIE 寄存器地址和位掩码。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portmacro.h` | 删除 `#include "cputimer.h"`。添加后备 `HWREGH()` 宏（volatile 16 位写入）。`portYIELD()` 用 `EALLOW`/`EDIS` 包裹，因为 PIE IFR 寄存器受 EALLOW 保护。 |
| `FreeRTOS-Kernel/portable/CCS/C2000_C28x/port.c` | 包含 `DSP2833x_Device.h` 而非 driverlib 头文件。`vPortSetupSWInterrupt()` 使用 `PieVectTable.rsvd1_3` + `PieCtrlRegs.PIEIER1.bit.INTx3` + `IER |= M_INT1`。`vPortSetupTimerInterrupt()` 使用 `CpuTimer2Regs` 直接写寄存器 + `PieVectTable.TINT2` + `IER |= M_INT14`。`#pragma WEAK` 已改为 `__attribute__((weak))`。 |

### 编译的内核源文件

- `FreeRTOS-Kernel/tasks.c` — 任务调度核心
- `FreeRTOS-Kernel/queue.c` — 队列
- `FreeRTOS-Kernel/list.c` — 链表（调度器内部使用）
- `FreeRTOS-Kernel/timers.c` — 软件定时器
- `FreeRTOS-Kernel/portable/MemMang/heap_4.c` — 堆内存分配（最佳适配）
- `FreeRTOS-Kernel/portable/CCS/C2000_C28x/port.c` — 移植层 C 部分
- `FreeRTOS-Kernel/portable/CCS/C2000_C28x/portasm.asm` — 移植层汇编（ramfuncs）

未编译的内核文件：`event_groups.c`、`stream_buffer.c`、`message_buffer.c`（如需要使用需手动加入工程）。

### 关键设计决策

- **Yield 中断**：PIE 第 1 组、第 3 号中断（`0x0103`）— F28335 保留中断。`portYIELD()` 宏通过手动置位 PIEIFR1 bit 2 触发，然后自旋 10 个周期等待 CPU 识别。这是 C28x 上 ARM PendSV 的等价实现。
- **Tick 中断**：CPU Timer 2 → INT14（非 PIE 中断）。周期 = `configCPU_CLOCK_HZ / configTICK_RATE_HZ`。
- **Tick 与 Yield 共享 `portTICK_ISR`** 作为 ISR。ISR 检查 `bYield`：若置位则为 yield 请求；若清零则先递增 tick 计数，然后（若 `bPreemptive` 置位且时间片到期）执行上下文切换。
- **临界区**：嵌套深度计数器方案（`ulCriticalNesting`）。`portENTER_CRITICAL()` 递增 → 关 INTM。`portEXIT_CRITICAL()` 递减 → 仅当计数器归零时重新使能中断。

### 栈初始化

`port.c` 中的 `pxPortInitialiseStack()` 在栈上伪造中断保存栈帧：将任务函数地址写入 PCL/PCH（待 POP 到 RPC），`pvParameters` 写入 XAR4 位置。`portRESTORE_FIRST_CONTEXT` 随后沿栈向下跳过，依次 POP XAR4 → POP RPC → LRETR，"恢复"到一个从未运行过的任务中。

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

## Flash API 库

`Library/Flash2833x_API_Library.asm` — TI F28335 Flash 编程 API，所有函数均位于 `.sect "ramfuncs"`，运行时从 FLASHC_0 拷贝到 RAML2。主要接口：

| 函数 | 作用 |
|------|------|
| `Flash28335_Erase` | 扇区擦除 |
| `Flash28335_Program` | 字编程 |
| `Flash28335_Verify` | 编程校验 |
| `Flash28335_ToggleTest` | 翻转测试 |
| `Flash28335_DepRecover` | 耗尽恢复 |
| `Fl28335_Init` | Flash 初始化 |
| `Fl28x_Delay` | Flash 控制器的精确延迟 |

`Flash_CPUScaleFactor`（`.ebss` 中）用于 `Fl28x_Delay` 的 CPU 频率缩放，`Flash_CallbackPtr`（`.text` 中）支持操作回调。

## 编译器特定注意事项

- **`#pragma WEAK`**：TI CGT 22.x 不接受 `WEAK` 与 `(` 之间有空格。改用 `__attribute__((weak))`，对 GCC/Clang/TI CGT 均可移植。
- **指针转整数**：C28x 中 `int` 为 16 位，指针为 32 位。指针类型转换必须用 `uint32_t`，切勿使用 `int`。
- **EALLOW 保护的 PIE 寄存器**：`PIEIFRx`、`PIEACK`、`PIEIERx` 和 `PieVectTable` 写入前均需 `EALLOW`/`EDIS`。driverlib API 内部已处理；直接寄存器访问必须自行处理。
- **`configSUPPORT_STATIC_ALLOCATION=1`** 时，需在应用中实现 `vApplicationGetIdleTaskMemory()` 和（若使用定时器）`vApplicationGetTimerTaskMemory()`。若 `configCHECK_FOR_STACK_OVERFLOW=2`，还需实现 `vApplicationStackOverflowHook()`。
- **COFF 符号命名**：C 变量在汇编中以带下划线前缀形式出现（`pxCurrentTCB` → `_pxCurrentTCB`）。EABI 不使用下划线。portasm.asm 按 COFF 编写（`__TI_EABI__` 未定义，EABI 适配块被跳过）。
- **Flash 运行限制**：擦除/编程 Flash 扇区时，执行该扇区内代码会导致总线挂起。所有 Flash API 函数（`.sect "ramfuncs"`）和 `portTICK_ISR` 均拷贝到 RAML2 执行，确保 Flash 操作期间 ISR 仍然可用。
- **F28335 上电复位 BUG**：`init.c` 中的 `PowerUpReset()` 处理 WDFLAG 采样问题——复位解除后需延迟 8192 个 SYSCLKOUT 周期再采样，否则 CPU 可能起不来。

## 源文件组织

```
Free_RTOS/
├── User/main.c                    # 应用入口 + LED 任务
├── FreeRTOSConfig.h               # FreeRTOS 配置（150MHz / 1000Hz tick）
├── FreeRTOS-Kernel/               # FreeRTOS 内核（tasks/queue/list/timers）
│   └── portable/
│       ├── CCS/C2000_C28x/        # C28x 移植层（port.c/portasm.asm/portmacro.h/portdefines.h）
│       └── MemMang/heap_4.c       # 堆管理
├── Include/                       # DSP2833x 位域结构体头文件
├── Library/                       # TI DSP2833x 库
│   ├── init.c                     # CPUInit() — 系统初始化 + ramfuncs 拷贝 + Flash 初始化
│   ├── DSP2833x_MemCopy.c         # MemCopy() — 启动时 Flash→RAM 拷贝
│   ├── DSP2833x_CodeStartBranch.asm  # codestart → _c_int00 跳转
│   ├── DSP2833x_GlobalVariableDefs.c # 外设寄存器结构体实例
│   ├── DSP2833x_DefaultIsr.c      # 默认中断服务例程
│   ├── DSP2833x_SysCtrl.c         # InitSysCtrl / InitFlash
│   ├── DSP2833x_PieCtrl.c/.PieVect.c/.Gpio.c/.usDelay.asm
│   ├── DSP2833x_ADC_cal.asm       # ADC 校准
│   ├── Flash2833x_API_Library.asm # TI Flash API（ramfuncs）
│   └── cmd/
│       ├── F28335_APP.cmd         # APP 链接脚本
│       └── DSP2833x_Headers_nonBIOS.cmd  # 外设寄存器映射
```

## CCS 头文件搜索路径

```
${PROJECT_ROOT}/FreeRTOS-Kernel/include
${PROJECT_ROOT}/FreeRTOS-Kernel/portable/CCS/C2000_C28x
${PROJECT_ROOT}/Include
${PROJECT_ROOT}
${CG_TOOL_ROOT}/include
```

## 依赖源文件来源

DSP2833x 库文件（`.c`/`.asm`）源于：
`d:\DSP\DSP28335\MCU Bare-metal\TI-C2000-TMS320F28335\Code\3_F28335_LED_Demo\Library\`

DSP2833x 头文件（`.h`）源于：
`d:\DSP\DSP28335\jjb\f28335_prj\include\`

使用 TI 传统的位域结构体寄存器访问模式（例如 `GpioDataRegs.GPATOGGLE.bit.GPIO0 = 1`），而非较新的 C2000Ware driverlib API。

## 相关文档

[FreeRTOS移植记录.md](FreeRTOS移植记录.md) — 完整移植日记（中文），包含所有设计决策的背景和原理。
