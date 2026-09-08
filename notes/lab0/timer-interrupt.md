# 材料三：一次时钟中断的微观旅程

> 从 `scause = 0x8000000000000005`（S 态时钟中断）进入，到 `sret` 返回用户态，中间发生的**全部动作**，以及引发的 `yield` 与调度决策。
> 要求见 [`docs/lab0-实验说明书.md` §材料三](../../docs/lab0-实验说明书.md#材料三一次时钟中断的微观旅程)。

**被中断的进程**：pid = ___，name = ___，中断前 `pc` 在用户代码的哪一行 = ___

---

## 时序阶段表

| # | 动作 | 执行者 | 栈 | 特权级 | 持锁 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 定时器到期，硬件置位 `sip.STIP` | HW | U-stack | U | — | |
| 1 | 硬件陷入：`sepc←pc`，`scause←0x80..05`，`sstatus.SPP←0`，`sstatus.SPIE←SIE`，`SIE←0`，`pc←stvec` | HW | | U→S | | 中断被自动关闭 |
| 2 | `uservec`（trampoline）：保存 32 个用户寄存器到 trapframe | SW | 尚无内核栈 | S | — | 用哪个寄存器做暂存？`sscratch` 怎么用？ |
| 3 | 切到内核页表、装载内核栈 sp | SW | → K-stack(p) | S | — | 为什么切页表后还能继续取指？ |
| 4 | `usertrap()`：改 `stvec` 指向 `kernelvec` | SW | K-stack(p) | S | — | 为什么必须改？ |
| 5 | 保存 `sepc` 到 `p->trapframe->epc` | SW | | S | | 为什么要存一份到内存？ |
| 6 | `devintr()` 识别为时钟中断，返回 2 | SW | | S | | 如何从 `scause` 判定？ |
| 7 | 时钟处理：`ticks++` + `wakeup(&ticks)` | SW | | S | `tickslock` | 唤醒了谁？ |
| 8 | `yield()`：`p->state = RUNNABLE` | SW | | S | `p->lock` | |
| 9 | `sched() → swtch(&p->context, &cpu->context)` | SW | K-stack(p) → sched-stack | S | `p->lock` **仍持有** | 为什么跨 swtch 持锁不是 bug？ |
| 10 | `scheduler()` 循环挑下一个 `RUNNABLE` 进程 | SW | sched-stack | S | | 挑选策略是什么？公平吗？ |
| 11 | `swtch` 回到（本次或另一进程的）`sched()` 之后 | SW | → K-stack(p') | S | | |
| 12 | `usertrap` 尾部调用 `prepare_return()`（旧名 `usertrapret`） | SW | K-stack(p') | S | | |
| 13 | 关中断、`stvec` 改回 `uservec`、填 trapframe 的 kernel_* 字段 | SW | | S | | 为什么这时候必须关中断？ |
| 14 | 设置 `sstatus.SPP=0`、`SPIE=1`，`sepc ← p->trapframe->epc` | SW | | S | | |
| 15 | 跳 `userret`：切用户页表、从 trapframe 恢复 32 个寄存器 | SW | | S | | 恢复顺序为何把 `a0`/`t6` 放特殊位置？ |
| 16 | `sret`：`pc←sepc`，`SIE←SPIE`，特权级←`SPP` | HW | → U-stack | S→U | — | 用户程序从被打断的**下一条**还是**同一条**指令继续？ |

---

## 三个必须讲清的点（验收检查项）

### ① 寄存器现场保护

- 保存在哪里？为什么不是内核栈而是 trapframe？
- trapframe 里除了 32 个通用寄存器还有什么？各自的作用：

_TODO_

### ② 调度器交接

- `swtch` 保存 / 恢复的 `context` 只有 `ra` + `sp` + 12 个 callee-saved，为什么够用？
- `p->lock` 的持有跨越了 `swtch`——是谁最终释放的？这条不变式（invariant）怎么表述？

_TODO_

### ③ `sret` 恢复

- 如果 `sepc` 没有正确恢复会怎样？（对比：系统调用路径需要 `+4`，中断路径**不需要**——为什么？）
- 如果 `stvec` 忘了改回 `uservec` 会怎样？

_TODO_

---

## 关联：这条路径后面归你实现

时钟中断链路在 **lab2（陷入、系统调用与控制台驱动）** 由你亲手写；`yield` / 调度决策在 **lab4（进程状态机与调度器）**。
调试时 `-d int` 日志里每 tick 一条 `0x8000000000000005` 属正常，用 `grep 'async:0'` 过滤掉才能看见真异常——见 [`docs/07-无gdb调试手册.md`](../../docs/07-无gdb调试手册.md#〇三个常识)。

---

## 💭 自主思考批注

> 💭 _TODO_

> 💭 _TODO_
