# lab0 思考题作答

思考题不判分、不收缴，但 lab1 验收问答直接从中抽选（[学生须知 §三](../../docs/05-学生须知.md#三每轮实验的标准工作流)）。

**参考树**：`reference/xv6-riscv` HEAD `9e3161a`。答案中的函数名、行号、字段均取自该树；与旧版 book 或网络资料不一致处，以参考树为准。

---

## 第一层（描述性）

### Q1. `fork` 返回后，父进程与子进程分别从哪一行继续执行？为什么两者的返回值不同？

**父进程**走的是普通系统调用返回路径：`kfork()`（`proc.c:259`）返回 `pid` → `syscall()` 把它写进 `p->trapframe->a0` → `usertrap()` 尾部 `prepare_return()` → `userret` → `sret`，回到用户态 `fork()` 调用点的下一条指令。

**子进程从未执行过 `kfork` 的任何一行。** 它的内核执行流起点由 `allocproc()` 预置（`proc.c:141–143`）：

```c
memset(&p->context, 0, sizeof(p->context));
p->context.ra = (uint64)forkret;
p->context.sp = p->kstack + PGSIZE;
```

所以子进程第一次被 `scheduler()` 选中、`swtch(&c->context, &np->context)` 恢复出 `ra = forkret` 时，它是**从 `forkret()` 的第一行开始**执行的。`forkret()`（`proc.c:513`）释放从 `scheduler` 手里继承来的 `p->lock`，然后 `prepare_return()`，再手动跳进 `userret`：

```c
uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
((void (*)(uint64))trampoline_userret)(satp);
```

`userret` 从 trapframe 恢复寄存器。子进程的 trapframe 是父进程的整份拷贝（`*(np->trapframe) = *(p->trapframe)`，`proc.c:279`），其中 `epc` 是父进程 `ecall` 的下一条指令地址——于是子进程"看起来"也是从 `fork()` 调用点返回的。

**返回值不同的机制**只有一行：

```c
np->trapframe->a0 = 0;    // proc.c:282
```

父进程的 `a0` 由 `syscall()` 写成 `pid`；子进程的 `a0` 在复制完 trapframe 后被显式改成 0。两者读的是各自 trapframe 里同一个偏移（112），值不同而已。

---

### Q2. `exec` 加载新镜像时，原进程的虚拟地址空间发生了什么变化？

**不是原地修改，而是"另建一张、成功后整体替换"。**

`kexec()`（`exec.c:28`）先 `proc_pagetable(p)` 建一张**全新**页表（只含 TRAMPOLINE 与 TRAPFRAME 两个映射），在新表上逐个 `ELF_PROG_LOAD` 段 `uvmalloc` + `loadseg`，再建 guard page 与用户栈、`copyout` 构造 `argv`。整个过程中 `p->pagetable` 仍指向旧表，进程的旧地址空间**完全没被触碰**。

直到全部成功，才在 `exec.c:132–138` 一次性提交：

```c
oldpagetable = p->pagetable;
p->pagetable = pagetable;          // 换表
p->sz = sz;
p->trapframe->epc = elf.entry;     // 新的入口
p->trapframe->sp = sp;             // 新的栈顶
proc_freepagetable(oldpagetable, oldsz);   // 最后才释放旧的
```

**任何一步失败都跳 `bad`**（`exec.c:142`），那里只 `proc_freepagetable(pagetable, sz)` 释放**新建的**那张，旧地址空间原封不动，`kexec` 返回 `-1`，进程继续正常运行——`user/sh.c` 里 `exec` 失败后还能 `fprintf(2, "exec %s failed\n", ...)` 就是靠这一点。

**`exec` 跨越保留下来的东西**：`p->trapframe` 页（同一物理页，只是被新表重新映射到同一虚拟地址 `TRAPFRAME`）、`p->kstack`、`p->ofile[]`、`p->cwd`、`pid`、`parent`、`p->context`。正因为 trapframe 页没换，`kexec` 才能把返回值 `argc` 通过 `p->trapframe->a0` 交给新程序。

---

## 第二层（机制性）

### Q3. `fork` 复制页表时，为什么最高处的 trampoline 页必须映射为只读，而不需要重新分配物理页？

先修正题面隐含的前提：**`uvmcopy` 根本不经手 trampoline。** `kfork` 调的是 `uvmcopy(p->pagetable, np->pagetable, p->sz)`，只遍历 `[0, sz)`；而 `TRAMPOLINE = 0x3F_FFFF_F000` 远在 `sz` 之上。子进程的 trampoline 映射来自 `allocproc()` → `proc_pagetable(np)`（`proc.c:189`）：

```c
mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X);
```

`trampoline` 是内核镜像里的符号，全系统只有这**一份物理页**（`kernel.ld` 把它对齐到页边界，`kvmmake()` 也把同一物理页映射到内核页表的同一虚拟地址）。

**为什么不需要重新分配物理页**：这一页的内容是纯代码（`uservec` / `userret`），所有进程完全相同，且运行期从不被写。更关键的是第 3 步 `csrw satp` 换页表那一刻：`pc` 正指在这页中间，只有当用户表与内核表把**同一物理页映射到同一虚拟地址**时，换表后的下一条指令才仍然有效。每进程复制一份副本不仅浪费内存，还会让"用户表与内核表指向同一物理页"这个前提失去意义。

**为什么必须只读**：`PTE_W` 缺席防止任何人改写陷入入口代码——它是特权级边界上唯一一段两边都能执行的代码，可写等于把内核入口交出去。至于 `PTE_U` 同样缺席，是因为访问它的时刻 CPU 已经在 S 态（硬件先切特权级、再跳 `stvec`），用户态自己永远碰不到这一页。

---

### Q4. Shell 的 `read` 系统调用返回之前，Shell 进程处于什么状态？最终是由谁将其唤醒的？

**状态**：`SLEEPING`，`p->chan == &cons.r`。

路径是 `consoleread()`（`console.c:96–108`）：缓冲区空（`cons.r == cons.w`）时执行

```c
sleep_prepare(&cons.r);
release(&cons.lock);
sleep();
```

`sleep_prepare` 在 `p->lock` 保护下把 `p->chan` 设为 `&cons.r`；随后释放 `cons.lock`；`sleep()` 重新取 `p->lock`，**只有在 `p->chan != 0` 时**才置 `SLEEPING` 并 `sched()`。

**唤醒者**：`consoleintr()`（`console.c:179–184`）。当收到 `'\n'`、`C('D')` 或缓冲写满时：

```c
cons.w = cons.e;
wakeup(&cons.r);
```

`consoleintr` 的调用链是：UART 硬件产生外部中断 → PLIC 上报 → `devintr()` 中 `plic_claim()` 得到 `UART0_IRQ`（10）→ `uartintr()` → 循环 `uartgetc()` 取字符 → `consoleintr(c)`。

`wakeup()`（`proc.c:577`）遍历 `proc[]`，对 `p->chan == chan` 的进程先把 `p->chan = 0`，再在 `state == SLEEPING` 时改成 `RUNNABLE`。

**两段式睡眠如何避免丢失唤醒**：旧版 `sleep(chan, lk)` 必须把调用方的锁传进去，靠"在持有 `p->lock` 的前提下释放 `lk`"实现原子性。本版改为：唤醒的标志不是"状态是否为 SLEEPING"，而是 **`p->chan` 是否已被清零**。即使中断恰好发生在 `release(&cons.lock)` 与 `sleep()` 之间，`wakeup` 也已经把 `p->chan` 置 0，`sleep()` 看到 `p->chan == 0` 就直接返回，不会睡死。

---

### Q5. echo 发起的第一次 `write` 系统调用，底层是如何一步步转化为 UART 硬件寄存器写入的？

每一跳靠什么找到下一跳，列在右列：

| 层 | 代码 | 依据 |
| --- | --- | --- |
| `write(1, "hi", 2)` | `user/echo.c` | — |
| `ecall` | `user/usys.S`（`usys.pl` 生成）先置 `a7 = SYS_write` | 系统调用号 |
| `uservec` → `usertrap()` | `stvec` 指向 trampoline | `prepare_return()` 预置 |
| `syscall()` | `num = p->trapframe->a7`；`syscalls[num]()` | 函数指针表 `syscall.c` |
| `sys_write()` | `argfd(0, 0, &f)` 取 `p->ofile[1]` | **fd 即 `ofile[]` 下标** |
| `filewrite(f, addr, n)` | `f->type == FD_DEVICE` 分支（`file.c:145`） | **`f->type`** |
| `devsw[f->major].write(1, addr, n)` | `f->major == CONSOLE == 1` | **`f->major` 作 `devsw[]` 下标**；表项由 `consoleinit()` 装填（`console.c:201`） |
| `consolewrite()` | 32 字节一批 `either_copyin` 到内核缓冲 → `uartwrite(buf, nn)` | — |
| `uartwrite()` | `acquiresleep(&tx_lock)`；循环中 `sleep_prepare(&tx_chan)`；查 `ReadReg(LSR) & LSR_TX_IDLE` | `LSR` 偏移 5，`LSR_TX_IDLE` 为 bit 5 |
| **`WriteReg(THR, buf[i])`** | 写物理地址 `UART0 + THR` = `0x1000_0000 + 0` | `memlayout.h:21`、`uart.c:26` |

若 `LSR_TX_IDLE` 未置位（THR 尚未腾空），`uartwrite` 走 `sleep()` 睡在 `tx_chan` 上，由 UART 发送完成中断触发的 `uartintr()` → `wakeup(&tx_chan)` 唤醒（`uart.c:143`）。

> 注意本版**没有发送环形缓冲区**，也没有 `uartstart()`：`uartwrite` 直接轮询并写 `THR`，写不进就睡。因此控制台输出是同步的——`write` 返回时字符已进入 THR。也正因为它会睡，`tx_lock` 必须是**睡眠锁**而非自旋锁。

---

## 第三层（预测性）

### Q6. 如果内核启动代码中创建第一个用户进程的调用被注释掉，系统会停在何处？现象是什么？

注释掉 `main.c:31` 的 `userinit()` 后，`proc[]` 全部保持 `UNUSED`。`main()` 继续走到 `scheduler()`（`main.c:44`），其主循环（`proc.c:435`）每轮扫完 64 个槽位都 `found == 0`，于是执行 `asm volatile("wfi")`，被时钟中断唤醒后再扫一轮，如此往复。

**预测的现象**：

- 内核自身的启动输出照常打完，然后停住
- **没有** `init: starting sh`，**没有** `$` 提示符
- 不 panic、不复位、不黑屏——这是"活着但永远无事可做"，与 lab1 的黑屏死机是两种完全不同的故障形态
- `fsinit(ROOTDEV)` 也不会执行：本版文件系统初始化在 `forkret()` 里（`proc.c:526`），而 `forkret` 只有进程被首次调度才会跑到

**验证手段**（对应调试手册三项）：

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d int -D int.log &
QPID=$!; sleep 3; kill $QPID
grep 'async:0' int.log | head     # 预期：空，没有任何异常
grep -c 'async:1' int.log         # 预期：大量，周期性时钟中断
```

QEMU Monitor（`Ctrl-a c` → `info registers`）应看到 `priv = S`、`pc` 停在 `scheduler` 的 `wfi` 附近；用 `addr2line -e kernel/kernel <pc>` 可确认落在 `proc.c` 的 scheduler 循环里。

> 这道题是"预测 → 实测 → 对账"的典型练习：改一行、跑一次、拿日志核对上面每一条预测。

---

### Q7. 如果子进程 `exec` 完成且已退出，而父进程尚未调用 `wait`，此时进程表中子进程处于什么状态？

**`ZOMBIE`**（`proc.c:358`）。

`kexit()` 已经释放的：

- 全部打开文件——遍历 `ofile[]` 逐个 `fileclose(f)` 并置 0（`proc.c:334–340`）
- 当前目录 inode——`begin_op(); iput(p->cwd); end_op(); p->cwd = 0`
- 它自己的子进程——`reparent(p)` 全部过继给 `initproc`

**仍然占用的**：

| 资源 | 为什么还留着 |
| --- | --- |
| `proc[]` 槽位 | 父进程要能扫到它 |
| `p->pid` | `wait` 的返回值 |
| `p->xstate` | 退出码，`kwait` 要 `copyout` 给父进程 |
| `p->trapframe` 页 | 由 `freeproc` 统一释放 |
| `p->pagetable` 与全部用户物理页 | 同上 |

真正的回收发生在父进程的 `kwait()` 里：扫到 `pp->state == ZOMBIE` → 取 `pid` → `copyout` 退出码 → `pp->parent = 0` → **`freeproc(pp)`**（`proc.c:398`），后者 `kfree` trapframe 页、`proc_freepagetable` 释放页表与用户内存，最后把 `state` 置回 `UNUSED`。

**若父进程先于子进程退出**：`kexit` 中的 `reparent()` 已把它过继给 `initproc`，而 `user/init.c` 的主循环里那个 `wait((int*)0)` 会收到任何"无主进程"的退出并忽略它——僵尸由 init 兜底回收，不会泄漏。

---

### Q8. echo 执行完毕调用 `exit`，它打开的标准输出文件结构是如何保证不发生资源泄漏的？

靠 **`struct file` 的引用计数** `f->ref`。

`kexit()` 遍历 `ofile[]`，对每个非空项调 `fileclose(f)` 并把槽位置 0。`fileclose()`（`file.c`）的关键是：

```c
if (--f->ref > 0) {
  release(&ftable.lock);
  return;               // 还有别人在用，到此为止
}
ff = *f;
f->ref = 0;
f->type = FD_NONE;      // 真正回收
...
begin_op(); iput(ff.ip); end_op();
```

echo 的 `ofile[0]`、`[1]`、`[2]` 指向**同一个** `struct file`（init 是用 `dup` 建立 stdout/stderr 的），所以退出时连减三次。按[材料二的推导](data-structures.md#f-ref-的推导)，`ref` 由 9 依次降到 6：

| 关闭 | `f->ref` |
| --- | --- |
| 退出前 | 9 |
| `fileclose(ofile[0])` | 8 |
| `fileclose(ofile[1])` | 7 |
| `fileclose(ofile[2])` | 6 |

**init 与 sh 手上的 stdout 完全不受影响**——这正是引用计数在"多进程共享同一个打开文件"场景下承担的职责：谁都不该替别人关掉它，也不该在自己退出时把它漏掉。只有当最后一个持有者关闭、`ref` 归零时，才 `iput(ff.ip)` 归还 inode 引用。

进程自身的内存（trapframe 页、页表、用户物理页）不在 `kexit` 里释放，而是留到父进程 `kwait` 中的 `freeproc()`——因为 `kexit` 最后要 `sched()` 走人，此时它**仍在自己的内核栈上运行**，不能把自己脚下的东西拆掉。

---

## 阅读中自行追加的问题

- 💭 `kexec` 在 `end_op()` 之后重新取了一次 `p = myproc()`（`exec.c:83`），而函数开头已经取过。中间的 `readi` / `begin_op` 可能睡眠并被调度到另一个核，`myproc()` 依赖 `tp` 寄存器——这是一处"睡眠之后不能沿用旧的 per-CPU 派生值"的实例。还有哪些地方存在同类约束？
- 💭 `scheduler()` 每轮循环顶部先 `intr_on()` 再立刻 `intr_off()`（`proc.c:441–442`），注释说是为了避免"所有进程都在等待时死锁"，同时防止中断与 `wfi` 之间的竞争。若把这两句删掉，在什么并发时序下会真的卡死？
- 💭 `wakeup()` 遍历整张 `proc[]` 并逐个 `acquire(&p->lock)`，`NPROC = 64`。时钟中断每 tick 都会 `wakeup(&ticks)`，即每秒约 10 次全表加锁扫描。在 lab4 自己实现调度器时，这个开销是否值得用等待队列替换？替换后 `sleep_prepare`/`sleep` 的两段式结构还成立吗？
