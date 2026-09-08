# lab0（热身）· 阅读与剖析：一条命令的生命周期

第 1 周。追踪 Shell 执行 `echo hi` 命令的全生命周期，交付三份核心图纸。
**通过制，不计分，但未通过者不能进入后续实验**；三份材料在 lab1 验收时随机抽验。

任务原文：[`docs/lab0-实验说明书.md`](../../docs/lab0-实验说明书.md)

---

## 交付材料

| 材料 | 文件 | 状态 |
| --- | --- | --- |
| 一：全系统控制流图（模块级） | [control-flow.md](control-flow.md) | 文字稿完成，待手绘 |
| 二：核心数据结构全景快照 | [data-structures.md](data-structures.md) | 文字稿完成，待手绘 |
| 三：一次时钟中断的微观旅程 | [timer-interrupt.md](timer-interrupt.md) | 文字稿完成，待手绘 |
| 思考题作答（8 道，不上交） | [thinking-questions.md](thinking-questions.md) | 完成 |

三份材料的文字部分基于参考树 `reference/xv6-riscv` HEAD `9e3161a` 通读后写成；快照数值取自 `memlayout.h` / `riscv.h` / `param.h` 常量与 `user/_echo` 的实际 ELF 布局，复核方法见[材料二文末](data-structures.md#复核方法)。

手绘图纸尚未绘制，扫描件放入 [`assets/`](assets/) 后在对应 md 中以 `![](assets/xxx.png)` 引用。

说明书预计耗时 10–12 小时：控制流图与阅读路线 6–8 小时，三份快照约 4 小时（快照部分可与 lab1 并行推进）。超过 10 小时仍未理清控制流图应带草稿求助。

---

## 阅读路线

按说明书推荐顺序推进，避免按文件字母序孤立阅读：

- [ ] 1. `user/sh.c` 的 `main` 循环 —— 先从用户视角看发生了什么
- [ ] 2. `user/usys.pl` + `kernel/syscall.c` —— 系统调用如何跨越特权级边界
- [ ] 3. `kernel/trampoline.S` + `kernel/trap.c` —— 特权边界上到底发生了什么（先读 C 分发逻辑，汇编只看注释与跳转）
- [ ] 4. `kernel/exec.c` + `kernel/vm.c::uvmcopy` —— echo 进程从何而来
- [ ] 5. `kernel/file.c` + `kernel/console.c` —— 字符串 `"hi"` 如何输出到外设
- [ ] 6. xv6 book《Operating system interfaces》《Traps and system calls》两章精读，与草图互校

**每个核心函数的三个追问**：它被谁调用？是否可能阻塞睡眠、若阻塞由谁唤醒？遇到错误分支时如何回滚并返回？

---

## 参考树与旧版资料的差异

说明书提示参考树是新版上游。通读后确认差异远不止命名，**一律以树内实际代码为准**：

| 旧版 / 网络资料 | 参考树 |
| --- | --- |
| `usertrapret()`，由它跳转到 `userret` | `prepare_return()`；且 `usertrap()` 改为**返回 satp**，返回后直接落入 `userret` |
| `sleep(chan, lk)` 单段 | `sleep_prepare(chan)` + `sleep()` 两段式 |
| `fork` / `exit` / `wait` / `exec` / `kill` | `kfork` / `kexit` / `kwait` / `kexec` / `kkill` |
| `printf()` | `printk()` |
| `userinit()` 加载内嵌 `initcode.S` | `userinit()` 只建进程，由 `forkret()` 调 `kexec("/init", …)` 从文件系统加载 |
| M 态 `timervec` + `mtimecmp` 转发软件中断 | **Sstc 扩展**，`clockintr()` 直接写 `stimecmp`，时钟中断不经 M 态 |
| UART 发送用环形缓冲 + 自旋锁 + `uartstart()` | `uartwrite()` 用睡眠锁 + `sleep_prepare(&tx_chan)`，无发送缓冲 |
| 惰性分配为习题 | `vmfault()` 已在上游，`usertrap` 处理 scause 13/15 |

逐条说明见 [control-flow.md](control-flow.md#命名与结构差异相对旧版-xv6-与网络资料)。

> 与 [Neyray/xv6-labs-2020](https://github.com/Neyray/xv6-labs-2020)（2020 年版 6.S081）的实测比对：该仓库仍属左列，`usertrapret` / `initcode` / `timervec` / `uartstart` 俱在，无 `prepare_return` / `sleep_prepare` / `stimecmp`。可作机理参照，不可照搬细节。

---

## 验收检查清单

教师在 lab1 验收时随机抽验，每项满足即认定通过：

- [ ] 控制流图完整覆盖 `read → fork → exec → write → exit` 全链路，调用逻辑完整自洽
- [ ] 控制流图中的核心跳转准确标注了堆栈归属与特权级转换
- [ ] 三张快照数据结构字段完整，引用链路清晰（`file → inode`、`proc → pagetable`）
- [ ] 页表快照正确标出 trampoline 与 trapframe 的虚拟地址边界及权限位
- [ ] 时钟中断流程完整体现寄存器现场保护、调度器交接与 `sret` 恢复
- [ ] 图纸中包含至少 3 处自主思考的批注痕迹（统一以 `💭` 标记，便于统计）

---

## 与后续实验的对应关系

| 本轮图纸内容 | 后续亲自实现的实验 |
| --- | --- |
| 控制流图中的特权级跨越与中断分发 | 实验 2 陷入、系统调用与控制台驱动 |
| 页表快照与虚拟地址转换树 | 实验 3 SV39 页表与物理内存管理 |
| `fork` / `exit` / `wait` 状态流转 | 实验 4 进程状态机与调度器 |
| 内存复制开销与写时复制机制 | 实验 5 写时复制 COW 与系统调用 |
| 文件描述符与文件表快照 | 实验 6 缓冲区缓存与日志文件系统 |
| 全系统架构图与故障定位 | 实验 7 综合故障排查与现场系统设计 |
