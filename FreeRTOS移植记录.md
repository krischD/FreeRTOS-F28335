# FreeRTOS V10.6.2 移植到 TMS320F28335 记录

> 日期: 2026-06-03  
> 目标平台: TMS320F28335 (C2000, 32-bit DSP, FPU32, 150MHz)  
> 编译器: TI CGT 22.6.1.LTS, COFF 格式  
> IDE: CCS 12.6.0  
> 内核版本: FreeRTOS V10.6.2 LTS (git tag `V10.6.2`)

---

## 一、为什么选 V10.6.2

**问题**: 在 DSP28335 上移植 FreeRTOS，选哪个版本合适？

**答案**: FreeRTOS V10.6.2 LTS。

- TI 官方 FreeRTOS 移植仓库 [c2000ware-FreeRTOS](https://github.com/TexasInstruments/c2000ware-FreeRTOS) 仅支持第3代及更新器件 (F28002x, F2838x 等)，F28335 不在官方支持列表
- 官方 C28x 移植代码位于 `Source/portable/CCS/C2000_C28x/`，基于 V10.4.x 内核
- V10.6.2 是 V10.x 系列最后的 LTS 版本，API 与 V10.4.x 兼容，无需修改内核核心代码
- V11.x 是新一代大版本，API 变更较大，不推荐首次移植使用
- SYS/BIOS 是 TI 官方推荐方案，但需额外工具链

---

## 二、需要哪些内核文件

### 核心 C 文件 (4 个，必选)
| 文件 | 作用 |
|------|------|
| `tasks.c` | 任务创建、调度、延时 — 内核核心 |
| `queue.c` | 队列、信号量、互斥量 |
| `list.c` | 双向链表，调度器数据结构基础 |
| `timers.c` | 软件定时器 |

### 头文件 (全部复制)
`include/` 目录下全部 18 个 `.h` 文件。

### 内存管理 (5 选 1)
选用 `heap_4.c` — 支持分配+释放+相邻空闲块合并，适合嵌入式 RTOS。

### 移植层文件 (4 个，来自 TI 仓库)
| 文件 | 作用 |
|------|------|
| `port.c` | C 层移植：任务栈初始化、启动调度器、配置节拍定时器 |
| `portasm.asm` | 汇编层：上下文保存/恢复、中断处理 |
| `portmacro.h` | 宏定义：数据类型、开关中断、临界区 |
| `portdefines.h` | PIE 中断号、寄存器地址宏 |

---

## 三、上下文切换机制 (关键问题)

### 问题 1: yield 中断的目的是什么？

FreeRTOS 通过**人为触发中断**来执行上下文切换。`portYIELD()` 做了三件事：

```c
#define portYIELD() {
    bYield = 0x1;                                     // 1. 设置标志
    HWREGH(PORT_PIE_O_FLAG) |= PORT_PIE_FLAG_YIELD;   // 2. 写 PIE IFR，人为触发中断
    asm(" RPT #9 || NOP");                             // 3. 等待 CPU 响应
}
```

**原因**: C28x 没有 ARM Cortex-M 的 `SVC`/`PendSV` 这样的 RTOS 专用软中断指令，只能通过写 PIE 标志寄存器的方式"伪造"中断。

### 问题 2: 上下文切换是在中断中一直执行的吗？

**是的**。整个上下文切换过程在 `portTICK_ISR` 这一个 ISR 内部完成，不退出中断：

```text
                     ┌──────────────────────────────────────┐
                     │          portTICK_ISR 这一个 ISR       │
   Task A ──入口──→ │ 保存 A 寄存器 → 存 A 的 SP → 选新任务   │
                     │ → 切 SP 到新任务栈 → POP 新任务寄存器   │
                     │ → IRET ──→ Task B (永远不会回到 Task A) │
                     └──────────────────────────────────────┘
```

- **保存旧任务**: `PUSH` 系列指令保存所有寄存器到当前栈
- **关键指令**: `MOVL *XAR0, XAR6` — 将 SP 存入 TCB
- **选新任务**: `LCR _vTaskSwitchContext` — 修改 `pxCurrentTCB` 指向新任务
- **切栈**: `MOV @SP, AR0` — SP 从旧任务栈切换到新任务栈
- **恢复新任务**: 所有后续 `POP` 从新任务栈读取
- **IRET**: 返回到新任务的断点

### 问题 3: 每个任务在栈上是否都有一个上下文块？

**是的**。每个任务有自己的独立栈空间，上下文块嵌在各自的栈里：

```
         物理内存
    ┌──────────────┐
    │  Task A 栈    │
    │ [RPC=A断点]   │ ← pxTopOfStack 指向这里
    │ [XAR4...]     │
    │ [FPU regs]    │
    │ [DP:ST1]      │
    ├──────────────┤
    │  Task B 栈    │
    │ [RPC=B断点]   │
    │ ...           │
    └──────────────┘
```

切换本质: `SP = newTCB->pxTopOfStack` — 改变栈指针就改变了"当前上下文"。

### 问题 4: 任务第一次还没运行时，栈上怎么会有上下文？

`pxPortInitialiseStack()` 在任务创建时**伪造**一份"被中断挂起过"的假现场：

- **任务入口地址** → 伪装成 RPC（返回地址）
- **pvParameters** → 伪装成 XAR4（参数寄存器）
- 其余寄存器填 0 或默认状态值

首次调度时 `portRESTORE_FIRST_CONTEXT` POP 这份假现场、`LRETR` 跳转，任务就开始执行了。

### 问题 5: C28x 与 ARM 上下文切换的区别

| | C28x | ARM Cortex-M |
|---|---|---|
| 触发方式 | 写 PIE IFR，伪造硬件中断 | 写 ICSR 寄存器，触发 PendSV 异常 |
| 寄存器保存 | CPU 不用自动保存，全部手动 PUSH | R0-R3,R12,LR,PC,xPSR 自动压栈，R4-R11 手动 |
| 切换点 | `IRET`（中断返回） | `POP {PC}`（直接跳转） |
| 本质 | 依赖中断框架 | 依赖异常框架 |

---

## 四、移植文件改造详解

### 4.1 portdefines.h

**改动**: 去掉 `#include "inc/hw_ints.h"`（driverlib 依赖），手动定义中断号。

```c
#define PORT_INT_YIELD  0x0103  // PIE Group 1, Interrupt 3 (F28335 保留中断)
```

编码格式: 高 8 位 = PIE 组号 (1-12)，低 8 位 = 组内中断号 (1-8)。

三个衍生宏自动计算:
- `PORT_PIE_ACK_YIELD` → PIEACK 位（清零对应组标志）
- `PORT_PIE_O_FLAG` → PIE IFR 寄存器地址
- `PORT_PIE_FLAG_YIELD` → IFR 寄存器中的位位置

### 4.2 portmacro.h

**改动**:
1. 删除 `#include "cputimer.h"` — driverlib 依赖
2. 添加 `HWREGH` 宏定义 — F28335 传统头文件没有这个
3. `portYIELD()` 加入 `EALLOW`/`EDIS` — PIE 寄存器受 EALLOW 保护

### 4.3 port.c — 重点改造

**头文件**: 替换为 `#include "DSP2833x_Device.h"`

**`vPortSetupSWInterrupt()`**: 用 F28335 PIE 操作替换 driverlib:
```c
EALLOW;
PieVectTable.rsvd1_3 = &portTICK_ISR;    // 注册 yield ISR
PieCtrlRegs.PIEIER1.bit.INTx3 = 1;       // PIE 层使能
EDIS;
IER |= M_INT1;                            // CPU 层使能
```

**`vPortSetupTimerInterrupt()`**: 用寄存器直接操作替换 driverlib:
```c
CpuTimer2Regs.PRD.all = configCPU_CLOCK_HZ / configTICK_RATE_HZ;  // 周期
CpuTimer2Regs.TCR.bit.TIE = 1;            // 使能中断
EALLOW;
PieVectTable.TINT2 = &portTICK_ISR;       // 注册 tick ISR
EDIS;
IER |= M_INT14;                           // CPU 级使能 INT14
```

### 4.4 portasm.asm

**改动**: PIEACK 写操作加入 `EALLOW`/`EDIS`:
```asm
EALLOW
MOV     @AL, #PORT_PIE_ACK_YIELD
MOV     *(0:0x0ce1), @AL
EDIS
```

**无需改动**: COFF 格式下不定义 `__TI_EABI__`，汇编中符号名已带下划线前缀。F28335 有 FPU32 (`.TMS320C2800_FPU32 = 1`)，走正确的代码路径。

### 4.5 为什么需要 EALLOW/EDIS？

F28335 的 PIE 寄存器（PIEIFR、PIEACK、PIEIER、PieVectTable 等）默认写保护。写之前必须执行 `EALLOW` 开锁，写完后 `EDIS` 上锁。TI driverlib 在 `Interrupt_register()` / `Interrupt_enable()` 内部已处理，绕过 driverlib 直接写寄存器就必须自己管。

---

## 五、FreeRTOSConfig.h 关键配置

| 宏 | 值 | 说明 |
|----|-----|------|
| `configCPU_CLOCK_HZ` | 150000000 | F28335 PLL 输出 |
| `configTICK_RATE_HZ` | 1000 | 1ms 系统节拍 |
| `configMINIMAL_STACK_SIZE` | 256 | 含 FPU 上下文 (~55 words) + 余量 |
| `configTOTAL_HEAP_SIZE` | 8×1024 | 8K 字 = 16KB，存于 `.ebss` 段 |
| `configMAX_PRIORITIES` | 8 | 测试够用 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 方法 2 — 创建时填充已知值 |

---

## 六、链接脚本修改

原 `28335_RAM_lnk.cmd` 将 RAML4-L7 各自独立。为容纳 8K 字 heap，将其合并为 16K 字连续块:

```c
/* MEMORY 部分 */
RAML4  : origin = 0x00C000, length = 0x004000  /* 0xC000-0xFFFF: 16K */

/* SECTIONS 部分 — 所有数据段指向合并后的 RAML4 */
.ebss    : > RAML4, PAGE = 1
.econst  : > RAML4, PAGE = 1
DMARAML4 : > RAML4, PAGE = 1
...
```

---

## 七、工程目录结构

```
Free_RTOS/
├── FreeRTOSConfig.h              ← FreeRTOS 配置
├── 28335_RAM_lnk.cmd              ← 链接脚本 (已修改)
├── Include/                       ← DSP2833x 头文件 (27个 .h)
├── Library/                       ← DSP2833x 库文件 (11个 .c/.asm/.cmd)
├── User/
│   └── main.c                     ← 测试工程 (2个 LED 闪烁任务)
└── FreeRTOS-Kernel/
    ├── include/                   ← FreeRTOS 头文件 (18个 .h)
    ├── tasks.c                    ← 任务调度
    ├── queue.c                    ← 队列/信号量
    ├── list.c                     ← 链表
    ├── timers.c                   ← 软件定时器
    └── portable/
        ├── CCS/C2000_C28x/        ← C28x 移植层
        │   ├── port.c
        │   ├── portasm.asm
        │   ├── portmacro.h
        │   └── portdefines.h
        └── MemMang/
            └── heap_4.c           ← 内存管理
```

## 八、CCS 工程配置

### 包含路径 (Include Paths)
```
${PROJECT_ROOT}/FreeRTOS-Kernel/include
${PROJECT_ROOT}/FreeRTOS-Kernel/portable/CCS/C2000_C28x
${PROJECT_ROOT}/Include
${PROJECT_ROOT}
```

### 需加入工程的源文件
- `FreeRTOS-Kernel/` 下: `tasks.c`, `queue.c`, `list.c`, `timers.c`
- `FreeRTOS-Kernel/portable/CCS/C2000_C28x/` 下: `port.c`, `portasm.asm`
- `FreeRTOS-Kernel/portable/MemMang/` 下: `heap_4.c`
- `Library/` 下: 全部 `.c` 和 `.asm` 文件
- `User/` 下: `main.c`
- 链接文件: `28335_RAM_lnk.cmd`, `Library/DSP2833x_Headers_nonBIOS.cmd`

---

## 九、测试验证

`main.c` 创建两个 LED 闪烁任务:

| 任务 | LED | 周期 | 优先级 |
|------|-----|------|--------|
| LED_Red | GPIO0 (D1, 红灯) | 500ms 翻转 | 1 |
| LED_Green | GPIO1 (D2, 绿灯) | 300ms 翻转 | 1 |

**预期效果**: 红灯和绿灯各自按不同频率独立闪烁，互不干扰 → FreeRTOS 调度正常运行。
