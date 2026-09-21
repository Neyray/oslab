# lab1 设计与验收问答

本文按实验说明书的五个核心思考题和现场三题问答组织。

## 阅读框架

1. **参数速查**：先记住本人的协议、栈和节流参数。
2. **启动主线**：能从 <code>0x1000</code> 连续讲到 S 态 <code>main</code>。
3. **核心思考题**：按任务书的五个问题逐项展开。
4. **现场三题速答与高频追问**：用于 5 至 8 分钟验收口述。
5. **故障定位**：按 QEMU 日志的 cause 值回答排查题。

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

## 2 任务书核心思考题

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

## 3 现场三题速答

### 3.1 机理题

复位后首条指令在 QEMU reset ROM 的 <code>0x1000</code>，<code>-bios none</code> 只是不加载 OpenSBI。RAM 从 <code>0x80000000</code> 开始，所以内核 ELF 的入口、链接地址和加载地址都必须落在 <code>0x80000000</code>。

### 3.2 个性化题

本人的协议号是 2。<code>kernel/console.c</code> 中的 <code>console_putc()</code> 累计正文逻辑字节，<code>kernel/main.c</code> 在输出 <code>[chk=12536]</code> 前暂停累计，避免 checksum 行把自身算进去。

### 3.3 定位题

启动栈定义在 <code>kernel/start.c</code>，容量直接引用 <code>kernel/course_sid.h</code> 的 <code>LAB1_STACK_KB=12</code>；<code>kernel/entry.S</code> 把数组低地址加 12 KiB 得到初始 <code>sp</code>。删除两条 PMP 配置后，<code>mret</code> 降入 S 态的第一条取指触发 instruction access fault，日志为 <code>cause=1</code>。

### 3.4 个性化题展开

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

## 4 高频追问

### 4.1 节流机制

~~~c
#define THROTTLE_PERIOD (16UL + (COURSE_SID % 16UL))
~~~

本学号得到 17，但源码不能硬编码 17。每发射一个物理字节，<code>physical_bytes</code> 加一；达到周期后执行由学号派生次数的真实 <code>nop</code> 循环，再把计数清零。协议 1 插入的点号也属于物理字节，因此应计入节流；协议 2 当前正文没有逐字符附加字节。

### 4.2 printf 边界

- <code>%d</code> 支持 0、负数、<code>INT_MIN</code> 和 <code>INT_MAX</code>。负数绝对值用无符号减法计算，避免对 <code>INT_MIN</code> 直接取负造成有符号溢出。
- <code>%u</code> 输出无符号十进制。
- <code>%x</code> 按 V2 任务书要求由格式化函数自身输出小写 <code>0x</code> 前缀，数字部分不补前导零；调用者不能再次手写前缀。
- <code>%ld/%lu/%lx</code> 使用 64 位 long，Banner 用 <code>%lu</code> 打印完整学号。
- <code>%p</code> 输出 <code>0x</code> 加无前导零的小写十六进制。
- <code>%s</code> 支持空字符串；空指针显示 <code>(null)</code>。
- <code>%c</code> 和 <code>%%</code> 分别输出字符与百分号。
- 未知格式原样输出百分号、可选的 <code>l</code> 和格式字符，避免静默丢失内容。

### 4.3 其他追问

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

## 5 故障现象与日志定位

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
