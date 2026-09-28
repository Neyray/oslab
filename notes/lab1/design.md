# lab1 设计与验收问答

依据 lab1 实验说明书 V2。第 1–2 节讲清“做了什么”，第 3 节逐题回答 4 道思考题（现场问答从这里抽），第 4–6 节应对追问和故障定位。本笔记同时存在于 `lab1-submit` 和 `lab2-submit` 两个标签下；`console.c`、`start.c`、`main.c` 在 lab2 中有增补，所以这三个文件按函数名定位，只给出两个版本中都不变的行号。

## 目录

1. [启动主线](#1-启动主线)
2. [逐文件实现要点](#2-逐文件实现要点)
3. [思考题](#3-思考题)
4. [个人参数相关问答](#4-个人参数相关问答)
5. [高频追问](#5-高频追问)
6. [故障现象与日志定位](#6-故障现象与日志定位)

## 1 启动主线

~~~text
QEMU reset ROM @ 0x1000                               M 态
  └─ _entry @ 0x80000000   (entry.S)                  M 态
       关中断 → mtvec 兜底 → 非 0 号 hart 停驻 → sp = boot_stack + 12 KiB
       └─ start()          (start.c)                  M 态
            MPP=S, mepc=main, 委托, PMP, satp=0, tp=hartid
            └─ mret
                 └─ main() (main.c)                   S 态
                      console_init → Banner → selftest → [chk=12536]
                      └─ printf → console_putc → uartputc_sync → UART THR
~~~

进入 `main` 时成立的条件：CPU 在 S 态、中断关闭、`satp` 为 Bare、PMP 放行 RAM 和 UART、`sp` 在 12 KiB 启动栈内、只有 hart 0 在执行 C 代码。

~~~text
0x00001000  QEMU reset ROM（复位后第一条指令）
0x10000000  UART0 MMIO：+0 THR/RBR，+1 IER，+5 LSR
0x80000000  RAM 起点 = 链接地址 = _entry，随后是 text/data/bss
            boot_stack[12288]，sp 初值在它的高地址端
~~~

任务书的“四个阶段”（输出 `A` → S 态输出 `B` → `Hello RISC-V OS!` → Banner）是开发时逐层确认的里程碑。最终内核只保留阶段四的输出。

## 2 逐文件实现要点

### entry.S：第一条指令，没有栈

| 步骤 | 指令 | 原因 |
| --- | --- | --- |
| 关中断 | `csrci mstatus, 8`；`csrw mie, zero` | 前者清全局 MIE，后者关所有 M 态中断源；此时没有栈和保存现场的代码 |
| 兜底向量 | `la t0, machine_trap_vector`；`csrw mtvec, t0` | Direct 模式，低两位是模式位，所以用 `.balign 4` 对齐 |
| 筛核 | `csrr a0, mhartid`；`bnez a0, .Lpark` | 只有一个启动栈，也没有锁，从核必须停住 |
| 建栈 | `la sp, boot_stack`；`li t0, LAB1_STACK_KB*1024`；`add sp, sp, t0` | 栈向下增长，初值放在数组末端 |
| 进 C | `call start` | `start()` 不返回；万一返回，落入 `.Lpark` |

从核停驻循环是 `wfi; j .Lpark`。`wfi` 允许被提前唤醒，所以后面必须跟跳转。

> 💭 栈大小在 `start.c` 数组定义和 `entry.S` 栈顶计算两处都直接用 `LAB1_STACK_KB` 宏。如果一处写宏、另一处写死 12288，改参数后 sp 会指到数组外面，而且不会报错。第一次调用深一点的函数时，才会把 bss 里别的变量踩坏。

### start.c：M 态配置后 mret

| 寄存器 | 写入值 | 作用 |
| --- | --- | --- |
| `mstatus.MPP` | 01（S） | 决定 `mret` 之后的特权级 |
| `mstatus.MIE` | 0 | lab1 没有可恢复的中断处理 |
| `mepc` | `main` | 决定 `mret` 之后的第一条指令 |
| `medeleg` / `mideleg` | `0xffff` | 把能委托的异常/中断交给 S 态 |
| `sie` | 0 | 委托了，但 S 态中断源一个都不开 |
| `pmpaddr0` | `0x3fffffffffffff` | NAPOT 编码，覆盖整个物理地址空间（`start.c` 的 `w_pmpaddr0`） |
| `pmpcfg0` | `0xf` | R=W=X=1，A=NAPOT，L=0（`start.c` 的 `w_pmpcfg0`） |
| `satp` + `sfence.vma` | 0 | Bare 模式，不做地址翻译 |
| `tp` | `mhartid` | 记下当前 hart 编号 |

lab2 在这里又加了 CLINT 定时器配置，`mtvec` 改指向 `timervec`。PMP、MPP、`mepc` 这条主线没有变，Banner 回归测试能证明这一点。

### console.c：两层输出

- `uartputc_sync()`负责**物理字节**：循环读 LSR，等到 bit5（THRE）为 1 再写 THR。MMIO 访问前后都加 `io_fence()`，最后调用 `throttle_after_byte()` 做个人节流。
- `console_putc()`负责**逻辑字符**：如果校验和统计处于开启状态，先累加字节值，再交给 `uartputc_sync`。协议 1 在这里多发一个 `.`。
- `console_checksum_reset/value/pause()` 分别是清零并开始统计、读取当前值、暂停统计。

> 💭 校验和统计的是逻辑字符，节流统计的是物理字节，两者故意分开。协议 1 插入的 `.` 会占用串口带宽，应该参与节流，但它不是正文内容，不应该算进协议内容。把协议逻辑放在 `console_putc`，UART 驱动就不需要知道学号协议是什么。

### printf.c：格式解析

- `print_unsigned()`：除基取余，余数先放进缓冲区，再倒序输出。
- `print_signed()`：负数先输出 `-`，再用 `0UL - (uint64)value` 求绝对值（`printf.c:41`），避免对 INT_MIN / LONG_MIN 取负时发生有符号溢出。
- `%x` 和 `%p` 由格式化函数自己输出小写 `0x`，数字部分不补前导零，0 输出为 `0x0`。
- `%s` 遇到 NULL 输出 `(null)`；遇到未知格式，原样输出 `%`、可选的 `l` 和格式字符，不静默吞掉。

### main.c：Banner

~~~c
console_checksum_reset();
printf("OSLAB1 sid=%lu mod97=%x\n", COURSE_SID, COURSE_SID % 97);
printf("selftest zero=%d neg=%d max=%d empty='%s' hex=%x long=%s\n", ...);
sum = console_checksum_value();
console_checksum_pause();          /* 否则 [chk=...] 自己也会被加进去 */
printf("[chk=%lu]\n", sum);
~~~

第二行 `selftest` 就是 printf 边界自测（T1-1a–f）。它会被计入校验和，所以不能随便删。`main()` 末尾的 `#ifdef LAB1_PRINTF_EXTRA_TEST` 是只有测试构建才会编译的额外格式用例，默认输出不受影响。

## 3 思考题

### 3.1 QEMU 复位后执行的第一条指令物理地址是多少？内核链接地址为何必须设置为 0x80000000？

**结论：** 第一条指令在 `0x1000`，是 QEMU 的复位 ROM，在 M 态执行。内核链接到 `0x80000000`，因为 QEMU `virt` 的 RAM 从这里开始。

- `-bios none` 只是不加载 OpenSBI，QEMU 自带的复位跳板仍然存在。它按内核 ELF 的入口 `_entry` 跳过去。
- 链接地址决定所有符号地址：`la` 取到的全局变量地址、`call` 的目标都依赖它。本实验没有重定位代码，所以链接地址必须等于 QEMU 实际装载的地址。
- 区分三个地址：复位地址（最初的 PC，`0x1000`）、链接地址（链接器假定的运行地址）、加载地址（QEMU 放置 ELF 段的位置）。后两者必须相等；复位地址可以不同，由 ROM 负责跳过去。
- 错误的后果：如果链接到 `0x0`，取指和全局变量访问会落到非 RAM 区域，结果是 access fault 或黑屏。

### 3.2 entry.S 中为何需要关闭中断并挂起从核？若在未配置运行环境时触发中断会产生什么后果？

**结论：** 入口处没有栈、没有可靠的 trap 向量，也没有保存现场的代码，这时来中断无法恢复。lab1 只有一个启动栈，也没有锁，从核必须挂起。

如果未配置就来了中断，CPU 会保存 `mepc/mcause` 并跳到 `mtvec`：

- `mtvec` 没设或没对齐：跳到错误地址，连续触发取指异常，表现为黑屏。
- 处理代码要用栈：`sp` 还是垃圾值，一压栈就写坏内存。
- 没有保存和恢复代码：即使进了处理函数，也回不到被打断的地方。

不挂起从核的后果：多个 hart 共用同一个 `boot_stack`，栈互相覆盖；同时修改 `checksum`、`physical_bytes`；同时写 UART，Banner 会重复或交错，校验和也不稳定。

### 3.3 若遗漏 PMP 配置，系统引导时会呈现何种现象？QEMU 日志中会记录何种异常信息？背后的硬件控制机制是什么？

**结论：** `mret` 之后串口全黑。`-d int` 日志里第一条是 `async:0 cause:1`（instruction access fault），epc 是 `main` 的地址。原因是 S/U 态的每次取指、读、写都要经过 PMP 检查，没有放行的表项就拒绝访问。

1. M 态能执行 `start()`，因为没锁定的 PMP 表项不约束 M 态。
2. `mret` 降到 S 态后，第一次取 `main` 的指令就被 PMP 拒绝，产生 cause 1。
3. 异常已委托给 S 态，但 lab1 没有 `stvec` 处理函数，之后可能继续跳飞。所以只看**第一条**同步异常。

PMP 按编号从低到高匹配，第一个匹配的表项的 R/W/X 位决定是否放行。本实现只用第 0 项覆盖全部地址。

> 💭 `satp=0` 只是关掉页表翻译，不会关掉 PMP。访问路径是“有效地址 → (Bare，不翻译) → 物理地址 → PMP 检查 → RAM/UART”。如果 PMP 只覆盖了 RAM、没覆盖 UART，内核能进入 `main`，要到第一次写 UART 时才报 cause 7。所以同样是黑屏，看第一条异常的 cause 和 epc 就能分出是哪一种。

### 3.4 写串口前轮询 LSR 寄存器第 5 位的目的何在？若不进行轮询直接连续写入，在何种场景下会导致数据丢失？

**结论：** LSR bit5 是 THRE（发送保持寄存器空），为 1 才能写下一个字节。CPU 写寄存器比串口按波特率往外发快得多，不等待就连续写，会覆盖还没发出去的字节。

- 容易丢数据的场景：真实硬件、低波特率、长字符串连续输出、FIFO 未启用或已满。QEMU 某一次没丢，不代表这样写是对的。
- 为什么只等 bit5 不等 bit6（TEMT）：THRE 表示能接收下一个字节，TEMT 表示整条发送链路都空了。连续发送只需要前者，等后者会白白降低吞吐。
- 条件写反的后果：UART 空闲时反而一直等，表现为黑屏、CPU 100%，但 `-d int` 里没有任何异常。这正是用日志区分它和 PMP 故障的依据。

## 4 个人参数相关问答

**问：你的协议是什么，怎么实现的？**
`2024302141121 % 3 = 2`，是校验和协议。`console_putc` 在统计开启时逐字节累加，`main` 打印完两行正文后先读出值、再暂停统计，然后打印 `[chk=12536]`。12536 是前两行（含换行符）所有字节 ASCII 码之和，自测用例 T1-2b 会在宿主机上重算验证。

**问：mod97 字段为什么是 `0x23`？**
`2024302141121 % 97 = 35 = 0x23`。任务书要求小写 `0x`、不补前导零。我的 `%x` 自带前缀，所以格式串里不能再写 `0x`，否则会变成 `0x0x23`。

**问：栈多大，在哪？**
12 KiB（`LAB1_STACK_KB=12`，12288 = 0x3000 字节），16 字节对齐，定义在 `start.c` 的 `boot_stack`。`entry.S` 用同一个宏计算栈顶。

**问：节流是怎么回事？**
`THROTTLE_PERIOD = 16 + SID % 16 = 17`，`THROTTLE_NOP_COUNT = 32 + SID % 32 = 33`。每发 17 个物理字节，执行 33 条真实的 `nop`。源码直接用 `COURSE_SID` 计算，不写死数字。

## 5 高频追问

**问：mret 做了什么？**
特权级 ← `MPP`，PC ← `mepc`，`MIE` ← `MPIE`。“跳到 main”和“降到 S 态”由同一条指令完成。

**问：委托了为什么 `sie` 还写 0，矛盾吗？**
不矛盾。委托决定“发生了交给谁处理”，`sie` 决定“这个中断源开不开”。lab1 先把归属关系定好，但所有中断源都关着。

**问：`satp=0` 时 0x80000000 是虚拟地址还是物理地址？**
Bare 模式下有效地址就是物理地址，所以两者相同。访问仍然要经过 PMP 检查。

**问：打印完为什么不退出？**
裸机没有上一层可以返回，进入 `wfi` 循环停住就是预期行为。验收脚本用 `timeout` 结束 QEMU。

**问：兜底 trap 向量为什么只停机，不 `mret`？**
lab1 没有保存现场，也不对异常分类，`mret` 回去只会反复触发同一个异常。停住可以保留现场，方便用 `-d int` 看出根因。

**问：为什么不用 UART 中断？为什么不做多核锁？**
lab1 只要求轮询输出，`console_init` 把 IER 写成 0；中断驱动输入是 lab2 的内容。只有 hart 0 进入 C 代码，启动路径是单核的，所以不需要锁。

**问：预置文件为什么不能改？**
`kernel.ld`、`riscv.h`、`types.h`、`memlayout.h`、`course_sid.h` 规定了硬件接口和个人参数，改了就等于绕开验收判据。自测用例需要换参数时，都是复制一份临时目录，只改副本。

## 6 故障现象与日志定位

~~~bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic -d int -D int.log &
QPID=$!; sleep 2; kill $QPID
grep 'async:0' int.log | head -n 3
~~~

| 日志 / 现象 | 故障性质 | 排查方向 |
| --- | --- | --- |
| `cause=1` | 取指访问异常 | PMP 基址、范围、X 位；`mepc` 是否指向 RAM |
| `cause=2` | 非法指令 | S 态执行了 M 态 CSR 指令；`mepc` 错 |
| `cause=5/7` | 读 / 写访问异常 | UART 基址和寄存器偏移；PMP 是否覆盖 MMIO |
| 无异常、CPU 100% | 死循环 | LSR 判断条件是否写反；从核停驻逻辑；`_entry` 是否导出并链接 |
| 屏幕看着对，`cmp` 失败 | 协议或格式问题 | 多或少换行、`0x` 前缀重复、校验和把自己也算了进去 |
