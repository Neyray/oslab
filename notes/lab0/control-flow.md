# 材料一：全系统控制流图（模块级）

> 从用户敲下回车，到屏幕显示 `hi` 为止。
> 要求见 [`docs/lab0-实验说明书.md` §材料一](../../docs/lab0-实验说明书.md#材料一全系统控制流图模块级)。

**标注三要素**（每个阶段都必须有）：

| 记号 | 含义 |
| --- | --- |
| 栈 | `U-stack` 用户栈 / `K-stack(pid)` 该进程内核栈 / `sched-stack(cpu)` 调度器专门栈 |
| 特权级 | `U` / `S` / `M` |
| 锁 | `held: <锁名>` / `held: —` |

---

## 手绘图

<!-- 扫描件放 assets/ 后在此引用 -->
<!-- ![控制流图](assets/control-flow.png) -->

_TODO：贴手绘图_

---

## 阶段分解表

> 逐行填。**栈 / 特权级 / 锁**三列是验收重点，不能空。

| # | 阶段 | 关键函数（文件:行） | 栈 | 特权级 | 持锁 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | sh 主循环读命令 | `user/sh.c: main → getcmd` | U-stack | U | — | |
| 2 | 陷入 `read` 系统调用 | `usys.S → ecall` | → K-stack(sh) | U→S | — | `scause=8` |
| 3 | trap 分发 | `kernel/trap.c: usertrap → syscall` | K-stack(sh) | S | | `sepc += 4` 在哪一步？ |
| 4 | 控制台无输入 → 阻塞 | `console.c: consoleread → sleep` | K-stack(sh) | S | `cons.lock` | 睡在哪个 chan 上？ |
| 5 | 让出 CPU | `proc.c: sched → swtch` | K-stack(sh) → sched-stack | S | | swtch 换的是哪些寄存器？ |
| 6 | UART 中断到达 | `trap.c: kerneltrap → devintr → uartintr` | | S | | |
| 7 | 唤醒 sh | `console.c: consoleintr → wakeup` | | S | | |
| 8 | sh 被调度回来，`read` 返回 | | | S→U | | 从 swtch 的哪一行继续？ |
| 9 | `fork` | `kernel/proc.c: fork → uvmcopy` | | | | 父子返回值如何区分？ |
| 10 | 子进程 `exec("echo")` | `kernel/exec.c: exec` | | | | 旧地址空间何时释放？ |
| 11 | 父进程 `wait` 阻塞 | `proc.c: wait` | | | | |
| 12 | echo `main → write(1,"hi")` | `user/echo.c` | U-stack(echo) | U | — | |
| 13 | `write` 陷入 → 文件层 | `sysfile.c: sys_write → filewrite` | | S | | `ofile[1] → file → ?` |
| 14 | 落到控制台设备 | `console.c: consolewrite → uartputc` | | S | `uart_tx_lock` | |
| 15 | UART 硬件输出 | `uart.c` MMIO 写 THR | | S | | 轮询 LSR 哪一位？ |
| 16 | echo `exit` | `proc.c: exit` | | | | 打开的 file 引用计数怎么回收？ |
| 17 | 父进程 `wait` 返回，回到 sh 主循环 | | | | | |

---

## 关键跳转的现场细节

### ① `ecall`：U → S 的那一瞬间

- 硬件自动做了什么：`sepc ←`、`scause ←`、`sstatus.SPP ←`、`pc ← stvec`
- 软件（trampoline）接手做了什么：
- 用户页表 → 内核页表的切换发生在哪条指令？切换前后哪些地址还有效？

_TODO_

### ② `swtch`：进程内核栈 → 调度器栈

- 保存 / 恢复的是 `context` 的哪些字段？为什么不需要保存 caller-saved 寄存器？
- `p->lock` 在 `swtch` 前后的持有情况：

_TODO_

### ③ `sret`：S → U 的返回

- `prepare_return`（旧名 `usertrapret`）做了哪些准备？
- `sret` 依赖哪些 CSR 才能正确回到用户态？

_TODO_

---

## 💭 自主思考批注

> 至少 3 处（本文件 + 另两份材料合计）。用 `💭` 便于统计。

> 💭 **疑问 1**：_TODO_

> 💭 **边界特例 2**：_TODO_

> 💭 **启发点 3**：_TODO_
