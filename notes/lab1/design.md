# lab1 设计与验收问答

本文按旧版实验说明书的五个核心问题、V2 的四道思考题和现场三题问答组织。相同知识点会交叉引用，但 V2 原题保留独立题号，便于现场抽题后直接定位。

## 目录

- [0 个性化参数速查](#0-个性化参数速查)
- [1 启动主线与内存布局](#1-启动主线与内存布局)
- [2 旧版任务书核心思考题](#2-旧版任务书核心思考题)
  - [2.1 为什么从 0x1000 启动却链接到 0x80000000](#21-为什么从-0x1000-启动却链接到-0x80000000)
  - [2.2 entry.S 每一步为什么存在](#22-entrys-每一步为什么存在)
  - [2.3 进入 S 态前必须完成什么](#23-进入-s-态前必须完成什么)
  - [2.4 satp 选择 Bare 还是恒等映射](#24-satp-选择-bare-还是恒等映射)
  - [2.5 UART 16550 如何轮询发送](#25-uart-16550-如何轮询发送)
- [3 V2 思考题完整回答](#3-v2-思考题完整回答)
  - [3.1 复位地址与内核链接地址](#31-复位地址与内核链接地址)
  - [3.2 关闭中断与挂起从核](#32-关闭中断与挂起从核)
  - [3.3 遗漏 PMP 的现象、日志与硬件机制](#33-遗漏-pmp-的现象日志与硬件机制)
  - [3.4 轮询 LSR bit 5 与数据丢失](#34-轮询-lsr-bit-5-与数据丢失)
- [4 现场三题速答](#4-现场三题速答)
- [5 高频追问](#5-高频追问)
- [6 故障现象与日志定位](#6-故障现象与日志定位)

建议先背第 0、1 节的参数与启动链路，再按抽到的题号进入第 2 或第 3 节；第 4 节适合短答，第 5、6 节用于应对追问和故障定位。

## 0 个性化参数速查

| 项目 | 结果 | 代码依据 |
| --- | --- | --- |
| 学号 | <code>2024302141121</code> | <code>COURSE_SID</code> |
| Banner 协议 | 2，末尾 checksum | <code>LAB1_BANNER_PROTOCOL</code> |
| 初始内核栈 | 12 KiB | <code>LAB1_STACK_KB</code> |
| 学号模 97 | <code>35 = 0x23</code> | Banner 十六进制字段 |
| 节流周期 | 17 个物理字节 | <code>16 + COURSE_SID % 16</code> |
| 当前 checksum | <code>12536</code> | Banner 与 selftest 的逻辑字节和 |

## 1 启动主线与内存布局

~~~text
QEMU reset ROM @ 0x1000 (M)
  └─ _entry @ 0x80000000 (M)
       ├─ 关闭 M 态中断
       ├─ mtvec ← machine_trap_vector
       ├─ 非 0 号 hart → park
       ├─ sp ← boot_stack_top
       └─ start()
            ├─ MPP ← S，MIE ← 0
            ├─ mepc ← main
            ├─ 配置 delegation、PMP、satp
            ├─ tp ← mhartid
            └─ mret → main (S)
                 └─ console_init → Banner/selftest → wfi
~~~

进入 <code>main</code> 时必须满足：CPU 已在 S 态，中断仍关闭，<code>satp</code> 为 Bare，PMP 允许 S 态访问内核与 UART，<code>sp</code> 位于 12 KiB 启动栈中，且只有 hart 0 执行 C 代码。

~~~text
低地址
0x00001000  QEMU reset ROM（复位后首条指令）
     ...
0x10000000  UART0 MMIO
     ...
0x80000000  RAM 起点、_entry、内核 text/data/bss
             boot_stack（12 KiB，向低地址增长）
             boot_stack_top ← sp 初值
高地址
~~~

V2 任务书把实现过程分成四个里程碑；这些是开发阶段现象，不是最终内核要依次打印的内容：

| 阶段 | 要证明的能力 | 当时的最小现象 |
| --- | --- | --- |
| 一 | 汇编入口、初始栈、C 调用和 MMIO 可用 | M 态直接输出 <code>A</code> |
| 二 | PMP、MPP、mepc 和 mret 正确 | S 态 <code>main</code> 输出 <code>B</code> |
| 三 | LSR.THRE 轮询和字符串输出稳定 | 完整输出 <code>Hello RISC-V OS!</code> |
| 四 | printf 与个人协议完成 | 最终 Banner 与 <code>expect_banner.txt</code> 一致 |

## 2 旧版任务书核心思考题

### 2.1 为什么从 0x1000 启动却链接到 0x80000000

**问：上电后 PC=0x1000 表示什么？**

答：QEMU <code>virt</code> 复位后先在 M 态执行位于 <code>0x1000</code> 的少量 reset ROM 代码。这里不是本实验内核的链接地址，而是 QEMU 固定的复位入口。

**问：-bios none 是否表示 0x1000 的 reset ROM 也不存在？**

答：不是。它表示不加载 OpenSBI 等外部 BIOS 或固件；QEMU 自身用于复位并把控制权交给 <code>-kernel</code> 镜像入口的最小 reset ROM 仍存在。

**问：为什么内核必须链接到 0x80000000，不能链接到 0x0？**

答：QEMU <code>virt</code> 的 RAM 从 <code>0x80000000</code> 开始，<code>kernel.ld</code> 也将内核 VMA 放在这里。链接地址决定 <code>_entry</code>、全局变量、<code>la</code> 和函数调用使用的地址；链接到 <code>0x0</code> 会使取指或数据访问落入并非 RAM 的区域。

**问：链接地址与加载地址有什么区别，为什么必须一致？**

答：链接地址是链接器为符号和重定位计算的运行地址；加载地址是 QEMU 按 ELF program header 放置代码的物理地址。本实验没有早期地址重定位机制，因此两者必须一致。

### 2.2 entry.S 每一步为什么存在

#### 关闭中断

<code>csrci mstatus, 8</code> 清除 <code>mstatus.MIE</code>，关闭 M 态全局中断；<code>csrw mie, zero</code> 关闭各类 M 态中断源。此时栈、trap vector 和后续 S 态环境尚未完成，任何异步中断都没有可靠的保存与恢复路径。

#### 设置 mtvec

<code>mtvec</code> 写入 <code>machine_trap_vector</code>，采用 Direct 模式。<code>mtvec</code> 低两位是模式字段，所以基地址必须至少 4 字节对齐；代码用 <code>.balign 4</code> 保证。早期异常处理只关闭中断并停驻，因为 lab1 尚未实现可恢复的 trap handler。

#### 只允许 hart 0 继续

<code>csrr a0, mhartid</code> 取得 hart 编号，非 0 号 hart 进入 <code>wfi</code> 和跳转组成的 park 循环。lab1 没有多核启动屏障，也没有保护全局 checksum、物理字节计数和 UART 输出的锁；多个 hart 同时初始化会产生重复或交错输出。

#### 建立初始栈

启动栈按 V2 任务书要求定义在 <code>kernel/start.c</code> 中：

~~~c
uint8 boot_stack[LAB1_STACK_KB * 1024]
  __attribute__((aligned(16)));
~~~

<code>LAB1_STACK_KB=12</code>，所以容量是 <code>12 KiB = 12288 = 0x3000</code> 字节。<code>entry.S</code> 先取得 <code>boot_stack</code> 的低地址，再加 <code>LAB1_STACK_KB * 1024</code> 得到初始 <code>sp</code>。RISC-V 栈向低地址增长，因此初值必须位于数组末端。栈按 16 字节对齐，满足 RISC-V ABI 的函数调用对齐要求。

<code>sp</code> 的初值和栈的分配表达式必须与 <code>kernel/course_sid.h</code> 中的 <code>LAB1_STACK_KB</code> 强一致；不能一处写宏、另一处硬编码 12 KiB。

#### 调用 start 和返回兜底

完成栈初始化后才可 <code>call start</code>。<code>start()</code> 按契约不会返回；若意外返回，也进入 park，而不是继续执行到未知指令。

### 2.3 进入 S 态前必须完成什么

| 配置 | 当前决策 | 原因 |
| --- | --- | --- |
| <code>mstatus.MPP</code> | 设置为 S | 决定 <code>mret</code> 的目标特权级 |
| <code>mstatus.MIE</code> | 保持 0 | lab1 尚无可恢复的中断处理路径 |
| <code>mepc</code> | 指向 <code>main</code> | 决定 <code>mret</code> 后第一条 S 态指令 |
| <code>medeleg/mideleg</code> | 写入 <code>0xffff</code> | 将硬件允许委派的异常和中断交给 S 态 |
| <code>sie</code> | 写 0 | 即使已委派，本实验也不启用 S 态中断源 |
| <code>pmpaddr0</code> | <code>0x3fffffffffffff</code> | NAPOT 地址编码覆盖物理地址空间 |
| <code>pmpcfg0</code> | <code>0xf</code> | <code>R=W=X=1</code>，<code>A=NAPOT</code>，<code>L=0</code> |
| <code>satp</code> | 0 | Bare 模式，不启用地址转换 |
| <code>tp</code> | <code>mhartid</code> | 保存当前 hart 编号 |

**问：为什么 delegation 和 sie=0 不矛盾？**

答：delegation 决定异常或中断发生时由哪个特权级处理，<code>sie</code> 决定是否启用具体的 S 态中断源。lab1 先建立正确的委派关系，但保持中断关闭。

**问：删除 PMP 两行会发生什么？**

答：现代 QEMU 要求 S/U 态物理访问显式落入 PMP 授权区域。删除 <code>w_pmpaddr0(...)</code> 与 <code>w_pmpcfg0(0xf)</code> 后，<code>mret</code> 虽能尝试降到 S 态，但 <code>main</code> 第一条指令取指立即触发 instruction access fault。现象通常是串口全黑，<code>-d int</code> 日志首个同步异常为 <code>cause=1</code>。

**问：为什么 PMP 使用 0xf？**

答：低三位 <code>R/W/X</code> 全为 1；地址匹配字段 <code>A=NAPOT</code>；锁定位 <code>L=0</code>。它与 <code>pmpaddr0</code> 的全范围编码配合，为 S 态提供本实验所需的取指、读写权限。

### 2.4 satp 选择 Bare 还是恒等映射

本实验选择 <code>satp=0</code>，即 Bare 模式。lab1 只需按物理地址执行内核并访问 UART，尚未要求页表、地址空间隔离或虚拟地址转换。恒等映射虽然也能保持虚拟地址等于物理地址，但需要先构造合法页表、设置根页表 PPN 并处理更多失败条件，不符合本轮最小启动目标。

写 <code>satp=0</code> 后执行 <code>sfence.vma</code>，清除可能残留的地址转换缓存状态。页表属于后续实验内容。

### 2.5 UART 16550 如何轮询发送

| 项目 | 值 |
| --- | --- |
| UART0 基址 | <code>0x10000000</code> |
| THR 发送保持寄存器 | 偏移 0 |
| IER 中断使能寄存器 | 偏移 1，本实验写 0 |
| LSR 线路状态寄存器 | 偏移 5 |
| THRE 标志 | LSR bit 5 |

<code>uartputc_sync()</code> 先反复读取 LSR，只有 <code>(LSR &amp; (1 &lt;&lt; 5)) != 0</code>，即 THR 为空时，才向 THR 写入字符。寄存器通过 <code>volatile</code> 指针访问，并在 MMIO 前后执行 I/O fence，防止编译器或 CPU 把访问错误重排。

启动早期没有时钟中断和调度器，因此采用无超时的忙等；代码假设 QEMU 16550 存在且最终可写。函数只负责一个物理字节，不实现学号协议。

## 3 V2 思考题完整回答

本节按 V2 任务书的四道原题独立作答。现场短答时先说每题的“结论”，老师继续追问时再沿“执行过程、故障现象、硬件原因”展开。

### 3.1 复位地址与内核链接地址

> **原题 1：QEMU 复位后执行的第一条指令物理地址是多少？内核链接地址为何必须设置为 0x80000000？**

**结论：** 第一条指令的物理地址是 <code>0x1000</code>；本实验内核链接到 <code>0x80000000</code>，因为 QEMU <code>virt</code> 机器的 RAM 从该物理地址开始，内核代码、数据、栈都必须放在实际可用的 RAM 中。

完整执行链如下：

1. CPU 复位后处于 M 态，<code>pc=0x1000</code>，先执行 QEMU 内置的最小 reset ROM。
2. <code>-bios none</code> 表示不加载 OpenSBI 等外部固件，不表示 QEMU 的复位跳板消失。
3. QEMU 根据内核 ELF 的 program header 把各段装入从 <code>0x80000000</code> 开始的 RAM，并由 reset ROM 把控制权交给 ELF 入口 <code>_entry</code>。
4. <code>kernel.ld</code> 以 <code>0x80000000</code> 为起点计算所有符号地址；代码进入 <code>_entry</code> 后，取指地址、<code>la</code> 得到的全局变量地址和函数调用目标因此都与实际加载位置一致。

这里要区分三个概念：复位地址是 CPU 上电后最初的 <code>pc</code>，链接地址是链接器计算符号和重定位时假定的运行地址，加载地址是 QEMU 实际放置 ELF 段的地址。本实验没有位置无关启动代码或早期重定位逻辑，所以链接地址和加载地址必须一致；复位地址则可以不同，因为 reset ROM 负责完成第一次跳转。

若误把内核链接到 <code>0x0</code>，符号和访存地址会落在 QEMU <code>virt</code> 的非 RAM 区域。常见结果是跳转后无法正常取指、在第一次全局数据访问时触发 access fault，或者直接黑屏，而不是“从地址 0 自动搬到 RAM”。

**现场追问：既然 satp=0，0x80000000 是虚拟地址还是物理地址？**

答：Bare 模式不做页表转换，S 态看到的有效地址直接作为物理地址使用，因此本实验中的 <code>0x80000000</code> 同时表现为链接使用的运行地址和实际 RAM 物理地址；但访问仍须通过 PMP 权限检查。

### 3.2 关闭中断与挂起从核

> **原题 2：entry.S 中为何需要关闭中断并挂起从核？若在未配置运行环境时触发中断会产生什么后果？**

**结论：** 关闭中断是为了在栈、trap 入口和特权级环境尚未建立时避免进入一个无法保存现场、无法恢复的处理流程；挂起从核是为了让尚无多核同步机制的 lab1 只由 hart 0 初始化全局状态和 UART。

<code>entry.S</code> 先清 <code>mstatus.MIE</code>，再把 <code>mie</code> 写 0：前者关闭 M 态全局中断开关，后者关闭机器软件、时钟和外部中断等具体来源。随后才设置四字节对齐的 <code>mtvec</code>、筛选 hart、建立 <code>sp</code> 并调用 C 函数。即使复位规范通常让中断处于关闭状态，也应由入口代码显式建立自己依赖的状态，不能依赖未声明的初值。

若在运行环境未配置时接收异步中断，硬件会保存 <code>mepc</code>、<code>mcause</code> 等 trap 状态并把 <code>pc</code> 改到 <code>mtvec</code> 指定的位置，随后可能出现以下后果：

- <code>mtvec</code> 尚未设置或地址未对齐：CPU 跳到错误地址，继续触发取指异常，表现为黑屏、反复 trap 或停死。
- <code>sp</code> 尚未建立：任何使用栈的处理程序都会向未知地址保存寄存器，造成访问异常或内存破坏。
- 上下文保存代码尚不存在：处理程序即使被执行，也无法可靠恢复被打断代码需要的寄存器和返回状态。
- 过早进入 S 态 trap：若异常已委派但 <code>stvec</code> 和 S 态 trapframe 尚未建立，同样会跳到无效入口，无法安全 <code>sret</code>。

挂起从核的流程是读取 <code>mhartid</code>，仅允许 <code>mhartid=0</code> 继续，其他 hart 在 <code>wfi; j</code> 循环中停驻。若不这样做，多个 hart 会共用同一个启动栈，并同时清零或修改 <code>physical_bytes</code>、<code>checksum</code> 等全局变量，还会并发写 UART，导致栈互相覆盖、Banner 重复或交错、checksum 不稳定。lab1 没有启动屏障、每核栈和 UART 锁，因此不能让从核进入 C 代码。

<code>wfi</code> 只是等待事件的节能提示，允许因实现原因提前返回，所以后面必须有跳转形成永久循环；不能只写一条 <code>wfi</code> 后让从核顺序落入 hart 0 的启动路径。

### 3.3 遗漏 PMP 的现象、日志与硬件机制

> **原题 3：若遗漏 PMP 配置，系统引导时会呈现何种现象？QEMU 日志中会记录何种异常信息？背后的硬件控制机制是什么？**

**结论：** 常见现象是 <code>mret</code> 后串口完全没有输出；<code>-d int</code> 日志首先可见同步的 instruction access fault，即 <code>async:0</code>、<code>cause=1</code>。根因是 CPU 降到 S 态后，每次取指、读和写都必须通过 PMP 物理访问权限检查，而没有匹配且授权的 PMP 表项时访问被拒绝。

故障链可以按时间顺序说明：

1. M 态能够执行 <code>start()</code>，因为未锁定 PMP 表项时 M 态通常不受该表项权限约束。
2. <code>mstatus.MPP</code> 已设置为 S，<code>mepc</code> 指向 <code>main</code>，所以 <code>mret</code> 会尝试在 S 态从 <code>main</code> 取第一条指令。
3. 若未写 <code>pmpaddr0</code> 和 <code>pmpcfg0</code>，该物理取指不在任何允许 S 态执行的区域内，硬件拒绝访问并产生 instruction access fault，异常号为 1。
4. 当前代码把可委派异常交给了 S 态，但 lab1 尚未建立完整的 <code>stvec</code> 和 S 态恢复路径，因此首个异常之后还可能继续跳到无效地址并出现重复异常；最稳定的定位依据是第一条同步异常记录。

查看 QEMU 日志时应关注：<code>async:0</code> 表示同步异常，<code>cause=1</code> 表示取指访问错误，<code>epc</code> 附近应是准备执行的 S 态入口，<code>tval</code> 在实现提供时记录出错地址。不要只看“黑屏”，因为错误的 UART 地址和反向的 LSR 判断也可能黑屏，但它们对应的首个异常或执行状态不同。

PMP 的硬件检查顺序是：CPU 得到一次取指、load 或 store 的物理地址后，按编号从低到高匹配 PMP 表项；第一个匹配表项的 <code>R/W/X</code> 位决定访问能否继续。当前实现将 <code>pmpaddr0</code> 写成覆盖物理地址空间的 NAPOT 编码，并将 <code>pmpcfg0=0xf</code>，即 <code>R=W=X=1</code>、<code>A=NAPOT</code>、<code>L=0</code>，因此 S 态既能执行 RAM 中的内核，也能读写 UART MMIO。

<code>satp=0</code> 只表示禁用虚拟地址翻译，并不会关闭 PMP。可以把访问路径记成：

~~~text
S 态有效地址 --Bare，无页表转换--> 物理地址 --PMP 权限检查--> RAM / UART
~~~

权限缺失时的异常可进一步区分：缺执行权限通常为 <code>cause=1</code>，缺读权限通常为 <code>cause=5</code>，缺写权限通常为 <code>cause=7</code>。若 PMP 只覆盖 RAM 而没覆盖 UART，内核可能已经进入 <code>main</code>，但会在第一次访问 UART MMIO 时产生 load/store access fault，而不是在第一条 S 态指令处失败。

### 3.4 轮询 LSR bit 5 与数据丢失

> **原题 4：写串口前轮询 LSR 寄存器第 5 位的目的何在？若不进行轮询直接连续写入，在何种场景下会导致数据丢失？**

**结论：** LSR 第 5 位是 THRE（Transmitter Holding Register Empty）。它为 1 表示发送保持寄存器能够接收下一个字节；每次写 THR 前轮询该位，可以保证软件不会在 UART 仍占用发送缓冲时塞入新字节。

16550 的发送路径可以简化为“CPU 写 THR／发送 FIFO → UART 把字节移入移位寄存器 → 按串行时序发出”。THRE 表示保持寄存器或 FIFO 已有接收空间，不等于线路上最后一位已经发送完毕；“发送器整体为空”通常由 LSR bit 6（TEMT）表示。本实验只需连续发送字节，因此等待 bit 5 已足够，无需等待 bit 6，否则会无谓降低吞吐量。

若不轮询就连续写 THR，CPU 写 MMIO 的速度可能远高于 UART 按波特率移出数据的速度。在真实串口低波特率、长字符串连续输出、目标机或仿真器暂时无法及时消费字符、发送 FIFO 未启用或已满时，后续写入可能覆盖尚未发送的数据或被设备丢弃，表现为缺字、乱码以及最终 Banner 逐字节比较失败。QEMU 某次运行“看起来没丢”不代表这种写法正确，因为这依赖模型和宿主机的消费时机。

正确的 <code>uartputc_sync()</code> 应对**每个物理字节**执行以下流程：

1. 通过 <code>volatile</code> MMIO 读取 <code>UART0+5</code>。
2. 当 <code>(LSR &amp; (1&lt;&lt;5)) == 0</code> 时继续等待。
3. THRE 为 1 后，将一个字节写入 <code>UART0+0</code> 的 THR。
4. 用 I/O fence 约束 MMIO 访问顺序，再进行物理字节计数和节流。

若把判断条件写反，UART 空闲时反而一直等待，通常没有同步异常，但 QEMU CPU 占用持续较高且串口无输出；若 LSR 偏移或 UART 基址写错，则可能读到无意义状态而永久忙等，也可能产生 <code>cause=5/7</code> 的访问异常。这正是验收时需要结合 <code>-d int</code> 日志区分的两类黑屏。

## 4 现场三题速答

### 4.1 机理题

复位后首条指令在 QEMU reset ROM 的 <code>0x1000</code>，<code>-bios none</code> 只是不加载 OpenSBI。RAM 从 <code>0x80000000</code> 开始，所以内核 ELF 的入口、链接地址和加载地址都必须落在 <code>0x80000000</code>。

### 4.2 个性化题

本人的协议号是 2。<code>kernel/console.c</code> 中的 <code>console_putc()</code> 累计正文逻辑字节，<code>kernel/main.c</code> 在输出 <code>[chk=12536]</code> 前暂停累计，避免 checksum 行把自身算进去。

### 4.3 定位题

启动栈定义在 <code>kernel/start.c</code>，容量直接引用 <code>kernel/course_sid.h</code> 的 <code>LAB1_STACK_KB=12</code>；<code>kernel/entry.S</code> 把数组低地址加 12 KiB 得到初始 <code>sp</code>。删除两条 PMP 配置后，<code>mret</code> 降入 S 态的第一条取指触发 instruction access fault，日志为 <code>cause=1</code>。

### 4.4 个性化题展开

**问：你的协议是什么，为什么？**

答：<code>2024302141121 % 3 = 2</code>，所以 <code>LAB1_BANNER_PROTOCOL=2</code>，采用末尾 checksum 协议。十六进制字段是 <code>2024302141121 % 97 = 35 = 0x23</code>，必须使用小写 <code>0x</code> 且无前导零。

**问：代码中哪个函数实现个人协议？**

答：协议层在 <code>kernel/console.c</code>：

- <code>console_putc()</code> 输出每个逻辑字节；checksum 开启时先把该字节按无符号值累加，再交给 <code>uartputc_sync()</code>。
- <code>console_checksum_reset()</code> 清零并开始累计。
- <code>console_checksum_value()</code> 读取最终值。
- <code>console_checksum_pause()</code> 停止累计。
- <code>kernel/main.c</code> 输出 Banner 和 selftest 后读取 checksum，暂停累计，再输出 <code>[chk=12536]</code>。

**问：为什么输出 checksum 前必须先 pause？**

答：否则 <code>[chk=...]</code> 自身的字符会继续改变 checksum，打印值与被校验的正文不再一致。

**问：协议层与硬件层为什么分开？**

答：<code>console_putc()</code> 定义逻辑字符如何按个人协议处理，<code>uartputc_sync()</code> 只负责一个物理字节何时能写入 THR。这样协议变化不会污染 UART 寄存器状态机，也能明确 checksum 统计逻辑字节、节流统计物理字节。

## 5 高频追问

### 5.1 节流机制

~~~c
#define THROTTLE_PERIOD (16UL + (COURSE_SID % 16UL))
~~~

本学号得到 17，但源码不能硬编码 17。每发射一个物理字节，<code>physical_bytes</code> 加一；达到周期后执行由学号派生次数的真实 <code>nop</code> 循环，再把计数清零。协议 1 插入的点号也属于物理字节，因此应计入节流；协议 2 当前正文没有逐字符附加字节。

### 5.2 printf 边界

- <code>%d</code> 支持 0、负数、<code>INT_MIN</code> 和 <code>INT_MAX</code>。负数绝对值用无符号减法计算，避免对 <code>INT_MIN</code> 直接取负造成有符号溢出。
- <code>%u</code> 输出无符号十进制。
- <code>%x</code> 按 V2 任务书要求由格式化函数自身输出小写 <code>0x</code> 前缀，数字部分不补前导零；调用者不能再次手写前缀。
- <code>%ld/%lu/%lx</code> 使用 64 位 long，Banner 用 <code>%lu</code> 打印完整学号。
- <code>%p</code> 输出 <code>0x</code> 加无前导零的小写十六进制。
- <code>%s</code> 支持空字符串；空指针显示 <code>(null)</code>。
- <code>%c</code> 和 <code>%%</code> 分别输出字符与百分号。
- 未知格式原样输出百分号、可选的 <code>l</code> 和格式字符，避免静默丢失内容。

### 5.3 其他追问

**问：程序输出完后为什么一直不退出？**

答：裸机内核没有宿主进程可返回，也没有关机协议；输出完成后进入 <code>wfi</code> 停驻循环是预期状态，不是卡死。验收脚本用超时结束 QEMU。

**问：为什么本实验不用 UART 中断？**

答：任务边界只要求轮询式输出；串口接收和中断驱动属于后续实验。<code>console_init()</code> 将 IER 写 0。

**问：为什么当前不实现多核锁？**

答：<code>entry.S</code> 已保证只有 hart 0 进入 C 代码，其余核 park，因此启动路径是单核、单上下文。多核并发保护不属于 lab1。

**问：为什么 trap vector 只 park，不尝试返回？**

答：lab1 没有 trapframe、异常分类和恢复上下文。盲目 <code>mret</code> 可能反复触发同一异常；停驻能保持现场并让 <code>-d int</code> 日志暴露根因。

**问：预置文件为什么不能改？**

答：<code>kernel.ld</code>、<code>riscv.h</code>、<code>types.h</code>、<code>memlayout.h</code> 和个性化参数文件定义统一的架构与验收边界。修改它们可能掩盖实现错误并破坏全班统一判据。

## 6 故障现象与日志定位

| 失误 | 预期现象 | 第一检查点 |
| --- | --- | --- |
| <code>_entry</code> 缺失 | 链接器给出入口警告，QEMU 无稳定输出 | <code>make</code> 日志 |
| <code>sp</code> 错或栈容量不一致 | 首次 C 调用附近异常或静默停止 | <code>entry.S</code>、符号地址 |
| PMP 缺失 | S 态第一条指令取指异常，<code>cause=1</code> | <code>grep 'async:0' int.log</code> |
| <code>mtvec</code> 未对齐 | CSR 写入不符合要求，异常跳转失控 | 向量地址低两位 |
| UART 基址或偏移错误 | 访问异常或永久忙等 | UART0、LSR、THR 地址 |
| <code>mepc</code> 或 MPP 错 | <code>mret</code> 后跳飞或特权级不正确 | <code>start.c</code> 与寄存器值 |
| 协议包装重复或漏输出 | 屏幕内容近似，但逐字节比较失败 | 比较实际输出与 expect 文件 |

V2 任务书给出的 QEMU 日志定位表还应熟记：

| 日志或现象 | 故障性质 | 排查方向 |
| --- | --- | --- |
| <code>cause=1</code> | 取指访问异常 | 检查 PMP 地址范围和执行权限 |
| <code>cause=2</code> | 非法指令异常 | 检查 S 态是否执行了 M 态专有 CSR 指令，以及 <code>mepc</code> 是否正确 |
| <code>cause=5/7</code> | 读或写访问异常 | 检查 UART 基址和寄存器偏移 |
| 无异常记录且 CPU 持续满载 | 停在死循环 | 检查 LSR bit 5 判断是否写反、从核 park 路径和入口是否正确链接 |
