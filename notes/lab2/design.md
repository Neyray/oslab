# lab2 设计与验收问答

依据 lab2 实验说明书 V2 和增量包《能力目标与接口约定》。第 1–3 节讲清“做了什么”，第 4 节逐题回答 3 道思考题，第 5 节回答增量包要求说明的设计决策，第 6 节列出与任务书不同的地方，第 7–8 节应对追问和故障定位。行号按当前 `labs/2024302141121-kernel/` 源码核对。

## 目录

1. [系统主线与四个阶段](#1-系统主线与四个阶段)
2. [两张控制流图](#2-两张控制流图)
3. [逐模块实现要点](#3-逐模块实现要点)
4. [思考题](#4-思考题)
5. [增量包设计决策](#5-增量包设计决策)
6. [与任务书不同的地方](#6-与任务书不同的地方)
7. [高频追问](#7-高频追问)
8. [故障现象与日志定位](#8-故障现象与日志定位)

## 1 系统主线与四个阶段

~~~text
_entry / start()                                   M 态
  lab1 的 PMP、MPP、mepc 不变；新增 CLINT 定时器与 timervec
  └─ mret → main()                                 S 态
       Banner（lab1 回归）
       vm_init      最小 SV39 映射：内核恒等映射 + TRAMPOLINE
       trap_init    stvec=kernelvec，PLIC，UART IER，sie.SEIE|SSIE
       proc_init    槽 0 装入 sh
       proc_enter_user → usertrapret → userret → sret
            └─ sh                                  U 态
                 read/write → ecall → 内核 → 返回
                 fork → wait → 子进程 exec(name) → exit → 父进程从 wait 返回
~~~

| 阶段 | 任务书目标 | 代码 | 验证 |
| --- | --- | --- | --- |
| 一 陷入框架 | uservec → usertrap → usertrapret → userret 往返 | `trampoline.S`（只读）、`trap.c:79 usertrap`、`trap.c:119 usertrapret`、`proc.h` trapframe | ecall 能返回到下一条指令 |
| 二 系统调用分发 | getpid/write/exit，未知调用号返回 −1 | `syscall.c:96 syscall` | `hi` 打出 pid，`badecall`、T2-3 通过 |
| 三 中断驱动输入 | UART 中断 → 环形缓冲 → read | `trap.c:62 trap_init`、`console.c:100 console_intr`、`console.c:157 console_read` | 敲键盘有回显，`bufstorm`、T2-1、T2-2 通过 |
| 四 装载 Shell | `_uprog_table` 找到 sh，第一次进入 U 态 | `proc.c:205 proc_exec`、`proc.c:246 proc_init` | 出现 `sh>`，能执行 `hi`、`spin` |

## 2 两张控制流图

### 2.1 一次 write 系统调用的完整往返

| # | 位置 | 特权级 / 栈 / 页表 | 动作与寄存器去向 |
| --- | --- | --- | --- |
| 1 | 用户桩 `write`（`usys.S`） | U / 用户栈 / 用户页表 | `a0=fd a1=buf a2=n a7=SYS_write(16)`，执行 `ecall` |
| 2 | 硬件 | U→S | `sepc`←ecall 地址；`scause`←8；`SPP`←U；`SPIE`←SIE，SIE←0；PC←`stvec`（uservec 的 TRAMPOLINE 地址） |
| 3 | `uservec` | S / 仍是用户 sp / 用户页表 | `csrw sscratch,a0` 暂存用户 a0；a0←TRAPFRAME；31 个寄存器存入 trapframe（a0 在偏移 112）；读出 `kernel_sp/kernel_trap/kernel_satp`；切 satp、换 sp，跳 usertrap |
| 4 | `usertrap()` | S / 进程内核栈 / 内核页表 | `stvec`←kernelvec；`epc`←`sepc`；scause==8 时 **`epc += 4`**（`trap.c:88`），然后调 `syscall()` |
| 5 | `syscall()` | 同上 | 调用号←`trapframe->a7`；`switch` 到 `sys_write`；`proc_copyin` 检查并复制用户缓冲区；逐字节 `console_putc`；返回值→`trapframe->a0` |
| 6 | `usertrapret()` | 同上 | 关中断；`stvec`←uservec；填写 trapframe 头部 4 个内核字段；`SPP`←U、`SPIE`←1；`sepc`←`epc`；调用 `userret(user_satp)` |
| 7 | `userret` | S→U | 切回用户页表；从 trapframe 恢复寄存器（a0 = 返回值）；`sret`，从 ecall 的下一条指令继续 |

### 2.2 一次键盘中断的完整路径

~~~text
按键 → UART 收到字节，IER=1 触发接收中断（IRQ 10）
  → PLIC：priority[10]=1，S 态 enable 打开 bit 10，threshold=0     trap.c:29 plic_init
  → CPU：sie.SEIE=1 且 sstatus.SIE=1
         （在用户态靠 usertrapret 设 SPIE=1；在内核等待时由 console_read 执行 intr_on）
  → 用户态进 uservec，内核态进 kernelvec；scause = 0x8000000000000009
  → device_interrupt()：claim 拿到 irq=10 → console_intr() → 把 10 写回完成 complete   trap.c:38
  → console_intr()（生产者）：LSR.DR=1 时循环读 RBR；'\r' 转成 '\n'；
         未满：入队，遇到 '\n' 时 lines++；已满：丢弃，dropped++，但保留换行；回显
  → console_read()（消费者）：之前在 wfi 里等；中断返回后 wfi 结束，
         重新检查 input_readable()，成立则取出字节 → proc_copyout → 返回用户
~~~

lab2 还没有真正的 sleep/wakeup，“唤醒”就是中断返回后 `wfi` 结束，读者重新检查条件。任务书允许这样做。它的局限是等待期间 CPU 不能去运行别的进程。lab4 要改成加锁的“检查条件 → 进入睡眠队列 → 原子地释放锁”，由中断处理函数调用 wakeup。

## 3 逐模块实现要点

### 3.1 trapframe（`proc.h`）

| 偏移 | 0 | 8 | 16 | 24 | 32 | 40–104 | 112 | … | 280 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 字段 | kernel_satp | kernel_sp | kernel_trap | epc | kernel_hartid | ra sp gp tp t0–t2 s0 s1 | a0 | a1–a7 s2–s11 t3–t5 | t6 |

汇编只认识数字偏移，比如 `sd a0, 112(...)`。`proc.c:36-39` 用编译期断言固定 `a0=112`、`t6=280`，字段顺序错了会直接编译失败。前 5 个字段由 `usertrapret` 在每次返回用户态前填好（`trap.c:129-132`），这就是任务书提示中“是谁在陷入之前把它们填好的”的答案。

### 3.2 usertrap / kerneltrap / usertrapret（`trap.c`）

- `usertrap`：一进来先把 `stvec` 改成 kernelvec，保存 `sepc`。之后分三路：cause 8 → `epc += 4` 后调 `syscall()`；设备中断 → `device_interrupt()`；其他 → 打印 `scause/sepc/stval` 后停机。
- `kerneltrap`：只接受 S 态下的设备中断。进入时保存 `sepc/sstatus`，返回前写回，防止嵌套 trap 把它们改掉。
- `usertrapret`：按任务书列出的 5 项职责逐一完成，最后跳到 TRAMPOLINE 里的 `userret`。

### 3.3 系统调用（`syscall.c`）

- `argraw(n)` 从 `trapframe->a0..a5` 取参数。
- 分发用 `switch`，`default` 返回 −1（`syscall.c:131`）。实现了 fork、exit、wait、read、exec、getpid、pause、uptime、write 这 9 个，其余 13 个编号都返回 −1。
- 写回返回值前先核对 `myproc() == caller`（`syscall.c:136`）。`exit` 和 `wait` 会切换当前进程，不核对的话，返回值会写进别的进程的 trapframe。
- `sys_write` 只接受 fd 1、2 和非负长度；`sys_read` 只接受 fd 0，长度 0 直接返回 0。用户缓冲区一律经过 `proc_copyin/copyout` 检查。

### 3.4 控制台输入（`console.c`）

~~~c
static struct {
  uint8 data[LAB2_BUF_SIZE];   /* 64 */
  uint64 read_index;           /* 单调递增，r */
  uint64 write_index;          /* 单调递增，w */
  uint64 lines;                /* [r, w) 中 '\n' 的个数 */
  uint64 dropped;
} input;
~~~

不变式：元素数 = `w - r`，且 0 ≤ `w - r` ≤ 64；空表示 `r == w`，满表示 `w - r == 64`；`lines` 等于未读区间里换行符的个数。数组下标取模。单核，访问下标时关中断，所以不需要锁。

两种语义的区别集中在 `input_readable()`（`console.c:138`）：

| | 行缓冲 `SEMANTICS=0` | 字符流 `SEMANTICS=1`（本人） |
| --- | --- | --- |
| 读者什么时候可以开始读 | `lines > 0`：缓冲区里有完整的一行 | `r != w`：缓冲区里有任意一个字节 |
| `read(0,&c,1)` 按下 `q` 未按回车 | 继续阻塞 | 立即返回 `q` |
| 生产者维护 | 入队 `\n` 时 `lines++` | 同左（计数始终维护） |
| 消费者维护 | 取出 `\n` 时 `lines--`，本次 read 结束 | 同左 |

多字节 read（例如 `bufstorm` 的 `read(0,buf,256)`）在两种语义下都读到换行或读满长度才返回，这样官方测试按“一次 read 一行”计数时结果才正确。

> 💭 这一段原来的写法是 `#if LAB2_BUF_SEMANTICS == 1 if (length == 1) break; #endif`。但当 `length == 1` 时，外层 `while (count < length)` 本来就会结束，这个分支是多余的：把宏改成 0，行为完全不变，“行缓冲”并不存在。现在把区别收拢到 `input_readable()` 这一个判断里，并用自测 A2 在语义 0 的副本上验证：按 `q` 不会返回，按回车之后才返回。

> 💭 缓冲区满时如果连换行也丢掉，超长行之后读者会一直等一个已经被丢弃的回车，`bufstorm` 就是这样卡住的。现在的规则是：换行覆盖最后一个未读字节，`lines` 跟着加 1（被覆盖的字节本来就是换行时不重复计数）。这样既允许丢字符，又保证读者总能遇到行尾。

### 3.5 中断源（`trap.c`、`start.c`、`timervec.S`）

- UART：`console_enable_interrupts` 设置 IER=1；`plic_init` 设置优先级、使能位和阈值；`trap_init` 打开 `sie.SEIE`。
- 时钟：这台 QEMU CPU 不支持 Sstc，访问 `stimecmp/menvcfg` 会触发 cause 2。所以由 M 态的 CLINT 定时器到期后进入 `timervec`：重新设置 `mtimecmp += 300000`，置 `sip.SSIP`，然后 `mret`。S 态收到 cause 1（软件中断），清掉 SSIP 并执行 `ticks++`。本轮不做抢占，`pause/uptime` 会用到 ticks。

### 3.6 进程与装载（`proc.c`）

- 静态 8 个槽，每槽有 64 KiB 用户内存、1 页 trapframe、1 张用户页表、8 KiB 内核栈。
- `proc_exec(name)`：去掉名字末尾的换行，遍历 `_uprog_table`（每项 `{u64 start; u64 end; char name[]}`，8 字节对齐，以 `{0,0}` 结尾）。找到后检查大小，清空 64 KiB，复制平铺二进制，设置 `epc=0`、`sp=64 KiB−16`。
- `proc_fork`：找空槽，复制整块用户内存和 trapframe，把子进程的 `a0` 设为 0，状态设为 RUNNABLE，返回子进程 pid。
- `proc_wait`：父进程设为 WAITING，切到它的 RUNNABLE 子进程；没有子进程就返回 −1。
- `proc_exit`：把子进程 pid 写进父进程的 `a0`，恢复父进程，释放子进程的槽。

> 💭 exec 从平铺二进制的地址 0 开始执行，但 `user.ld` 里的 `ENTRY(main)` 在 `objcopy -O binary` 之后就丢了。写自测程序 `syserr` 时，GCC 把静态辅助函数排在了 `main` 前面，结果 exec 之后直接执行辅助函数，程序卡住。所以自测里加了 A1 检查：所有内嵌程序的 `main` 都必须在地址 0。`syserr` 改成只保留 `main` 一个函数，其余用宏实现。lab5 如果改用 ELF 装载，就应该读取 `e_entry`，而不是假定入口是 0。

## 4 思考题

### 4.1 ecall 与 sret 执行时硬件各做了什么？RISC-V 为什么不在硬件层面自动保存通用寄存器、切换栈？

**结论：** ecall 只做 4 件事：记录 `sepc`、记录 `scause`、保存特权级和中断状态、跳到 `stvec`。sret 只做 3 件事：恢复特权级、恢复 SIE、PC ← `sepc`。两者都不碰通用寄存器，也不换栈。

- ecall：`sepc`←ecall 自身地址；`scause`←8；`SPP`←U，`SPIE`←SIE，SIE←0；特权级切到 S，PC←`stvec`。
- sret：特权级←`SPP`；SIE←`SPIE`；PC←`sepc`。寄存器由 `userret` 事先恢复好。
- 为什么不让硬件自动保存：这样 trap 入口简单、延迟低；而且不同 OS 的栈布局（每核栈还是每进程栈）、需要保存哪些寄存器各不相同，硬件定死反而不灵活。代价是软件必须提供 trampoline。
- trampoline 必须用汇编：刚进来时 `sp` 还是用户的，C 函数开头第一件事就是用 `sp` 压栈；所有通用寄存器也都属于用户，只能先借 `sscratch` 腾出一个寄存器。

### 4.2 处理系统调用时要对 sepc 做什么处理？为什么？时钟中断的处理需要对 sepc 做同样的处理吗？为什么？

**结论：** 系统调用要 `sepc += 4`，因为 `sepc` 指向 ecall 自己，不加就会回去重复执行 ecall。时钟和 UART 中断不能加 4，因为异步中断发生在指令边界，`sepc` 本来就是下一条该执行的指令。

- 不加 4 的现象：返回后又执行 ecall，陷入死循环，write 会反复输出同一串字符（`sh> sh> sh> ...`）。
- 本实现只在 `cause == 8` 分支里加（`trap.c:87-88`），而且在调用 `syscall()` 之前加。这样即使 syscall 里发生了进程切换，trapframe 里保存的也已经是正确的恢复点。
- 非法指令、访问越界也不能加 4：那等于跳过错误继续运行，会掩盖问题。本实现的做法是打印 `scause/sepc/stval` 后停机。

### 4.3 行缓冲与字符流两种语义，在中断处理和读取函数里各造成什么差别？如果只改一处会出什么问题？

**结论：** 生产者的“通知条件”和消费者的“开始读条件”必须是同一个谓词。行缓冲的谓词是“有完整的一行”，字符流的谓词是“有任意一个字节”。

- 中断处理（生产者）：行缓冲只在收到 `\n` 时通知读者，字符流每收到一个字节都通知。本实现的 `console_intr` 始终维护 `lines`，这正是行缓冲的通知条件。
- 读取函数（消费者）：`input_readable()` 按宏选择谓词（见 3.4 的表）。
- 只改中断侧（每个字符都通知），读者却坚持等完整的一行：读者被频繁唤醒，检查后又睡回去，全是无效唤醒。
- 只改读取侧（来一个字就返回），生产者却只在回车时才通知：字符已经在缓冲区里，读者却一直睡到回车，字符流退化成了行缓冲。
- lab2 用 `wfi`，任何中断都会让读者重新检查条件，所以第一种错误只是白白消耗 CPU。到 lab4 改成 sleep/wakeup 以后，第二种错误会变成真正的“丢失唤醒”。

## 5 增量包设计决策

| 问题 | 本实现的做法 | 取舍 |
| --- | --- | --- |
| trapframe 的存储位置与大小？每进程还是全局？谁在何时切换？ | 每进程一页 `trapframe_pages[8]`，所有用户页表都映射到同一个虚拟地址 TRAPFRAME。`usertrapret` 填头部，uservec 保存寄存器。fork 复制整个 trapframe 后把子进程 `a0` 改成 0 | 每进程独立，fork/wait 切换时不需要额外保存寄存器 |
| 输入缓冲结构与锁？“检查状态-等待”如何实现？两种语义体现在哪几行？ | 单调下标环形缓冲 + `lines` 计数；单核关中断，不用锁；等待用 `intr_on; wfi; intr_off` 循环；语义区别在 `console.c:138-146 input_readable()` | 实现最简单；lab4 必须换成睡眠队列 |
| 分发表形式与错误码？ | `switch`，`default` 返回 −1；写回前核对调用者 | 系统调用少时边界清楚；调用多了以后可以改成带范围检查的函数表 |
| 没有页分配器时 fork 怎么复制？复制到哪、谁记账？ | 复制到空闲槽的静态 64 KiB 数组；`struct proc` 的 `state/parent_pid` 记账 | 立即完整复制，没有 COW；最多 8 个进程 |
| 用户指针能否直接解引用？ | 没有直接解引用：`proc_copyin/copyout/copyinstr` 检查“地址 + 长度”不超出 64 KiB | lab3 要改成逐页 walk，并检查 PTE_U / PTE_W |

## 6 与任务书不同的地方

1. **开了最小 SV39 页表，而不是 `satp=0`。** 预置 `trampoline.S` 固定访问高地址 TRAMPOLINE/TRAPFRAME，`user.ld` 又把程序链接在地址 0，这两个地址都不是物理 RAM。`vm_init`（`proc.c:116`）给内核建两个 1 GiB 大页做恒等映射，再加 trampoline；`setup_user_pagetable`（`proc.c:81`）为每个进程映射 VA 0 起的 64 KiB、trampoline 和 trapframe。这不是 lab3 的通用分配器。
2. **程序没有加载到 `end` 之后。** 程序复制到内核 bss 里的 `user_memory[slot]`，再映射到 VA 0。这块内存由链接器分配，同样不会覆盖内核。
3. **时钟走 M 态桥接**，S 态看到的是 cause 1，不是 cause 5（见 3.5）。
4. **fork 之后子进程不会立刻运行**，父进程调用 wait 时才切过去。这是最小的执行流切换，不是调度器。
5. **构建时重命名 trampoline 所在的段。** 预置 `trampoline.S` 使用 `trampsec` 段，而预置链接脚本只收 `.text/.text.*`。Makefile 用 `objcopy --rename-section` 把它改名为 `.text.trampoline`，两个预置文件都没有改动。

## 7 高频追问

**问：`sscratch` 在这里起什么作用？**
刚进 uservec 时，所有寄存器都是用户的。先把用户 a0 存进 `sscratch`，腾出 a0 装 TRAPFRAME 地址；其余寄存器都存完后，再把原来的 a0 取回来，写到偏移 112。

**问：为什么用户态和内核态的 `stvec` 不一样？**
用户态 trap 要走 uservec：换栈、换页表、保存进 trapframe。内核态 trap（例如 console_read 等待时来了中断）已经在可信的内核栈上，走 kernelvec 在当前栈上压 256 字节即可。如果内核态也用 uservec，内核现场会被当成用户现场写进 trapframe。

**问：trampoline 为什么要在两张页表里映射到同一个虚拟地址？**
uservec/userret 执行到一半会切换 `satp`，下一条指令仍然按当前 PC 取指。两张页表对这一页的映射必须相同，执行流才能接得上。

**问：fork 之后父子进程分别拿到什么？谁先运行？**
父进程拿到子进程 pid，子进程拿到 0。父进程先继续运行，调用 wait 时才切到子进程。子进程 exit 后，父进程从 wait 返回子进程 pid。sh 的 pid 是 1，第一个子进程是 2。

**问：为什么 spin 运行时 Shell 不能输入下一条命令？**
sh 正在 wait，而 spin 永远不 exit。lab2 只要求 spin 期间中断仍能响应（敲键盘有回显），并发运行多个进程是 lab4 的事。

**问：未知调用号为什么不会让内核崩？用函数表要注意什么？**
`default` 返回 −1。如果用函数表，必须先检查编号范围和表项是否为空，否则 `table[99]` 会越界读出一个随机指针并跳过去。T2-3 专门测了 0、23、−1 这几个紧贴合法范围边界的编号。

**问：缓冲区满时为什么不在中断里等？**
消费者要等中断返回后才有机会运行，中断里等待就是死锁。所以只能丢弃。

**问：PLIC claim 之后为什么必须 complete？**
claim 会占住这个中断源，把同一个编号写回 complete 后，PLIC 才会放行它的下一次中断。忘了 complete，第一次按键之后就再也收不到中断。

**问：bufstorm 的 bytes 为什么每次不一样？**
取决于读者能不能边收边取。第一行 100 个 x 如果把 64 字节缓冲区填满，就是 63 个 x + 换行，再加 two/three/four 三行共 15 字节，总共 79；如果读者来得及取走，一个都不丢，就是 116。官方说明写明，超长行丢字符属于预期行为。

**问：lab2 改了 start.c，会影响 lab1 吗？**
只加了定时器配置，PMP、MPP、mepc 这条主线没变。Banner 回归（前 3 行逐字节一致）和 lab1 自测 23 项都通过。

**问：后面哪些地方要重构？**
lab3：用户指针检查改成逐页 walk，静态页表改成通用分配器。lab4：`wfi` 等待改成 sleep/wakeup，wait/exit 的直接切换改成真正的调度器，`input_readable()` 变成 wakeup 条件。lab5：在 usertrap 里加缺页分支（cause 12/13/15），exec 支持 argv，按 ELF 入口启动。lab6：`_uprog_table` 换成文件系统。

## 8 故障现象与日志定位

~~~bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic -d int -D int.log
grep 'async:0' int.log | grep -v 'cause:0000000000000008' | head      # 排除正常的 ecall
riscv64-unknown-elf-addr2line -e kernel/kernel <epc>
~~~

| 症状 | 首先该问的问题 | 检查位置 |
| --- | --- | --- |
| write 后反复输出同一串 | 返回后 PC 是否还停在 ecall | `trap.c:88` 的 `epc += 4` |
| 非法调用号让内核崩了 | default 去哪了，返回值放在哪 | `syscall.c:131`、`:136` |
| 一进用户态就 cause 1/2 | epc、映射、栈对不对 | `epc=0`、用户页表 VA 0 带 PTE_U、trampoline 页 |
| exec 之后程序卡住、没有 ecall | 地址 0 是不是 `main` | 自测 A1；`riscv64-unknown-elf-nm -n user-flat/x.elf` |
| 敲键盘没有回显 | 中断使能链路哪一环断了 | IER → PLIC 优先级/使能/阈值 → `sie.SEIE` → `SIE/SPIE` → complete |
| 输入乱码或丢字 | 下标和空满判断对不对 | 取模位置；空满条件是否写反；访问下标时是否关了中断 |
| 超长行之后永远卡住 | 换行是否也被丢了 | `console_intr` 满缓冲分支 |
| 启动时 cause 2 | 是否访问了不支持的 CSR | 不要用 `stimecmp/menvcfg`，走 M 态桥接 |
| userret 在高地址 cause 1 | trampoline 是否在可加载段里 | Makefile 的段重命名；PTE_X |
