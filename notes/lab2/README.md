# lab2 陷入、系统调用与中断控制台

目标：让内核能运行用户程序。用户程序通过 `ecall` 陷入内核请求服务；键盘输入经 UART 中断进入内核缓冲区；最终第一个用户程序 Shell 跑起来，出现 `sh>`，能执行 `hi`、`spin`。

| 文档 | 内容 |
| --- | --- |
| [design.md](design.md) | 设计与问答：两张控制流图、逐模块实现、3 道思考题、5 项设计决策、追问、故障定位 |
| [test-plan.md](test-plan.md) | 验收清单：现场命令与通过标准；3 个自测用例及实测结果 |
| [`tests/lab2_selftest.py`](../../labs/2024302141121-kernel/tests/lab2_selftest.py) | 自测用例驱动脚本 |
| [`support/tests/`](../../labs/2024302141121-kernel/support/tests/) | 官方测试 `badecall`、`bufstorm` 及脚本 |

## 个人参数与自定常量

| 名称 | 值 | 作用 |
| --- | ---: | --- |
| `LAB2_TICK` | 3 | 时钟间隔 = 100000 × 3 个 timebase 单位 |
| `LAB2_BUF_SEMANTICS` | 1 | 字符流：缓冲区里有任意一个字节，读者就可以开始读 |
| `LAB2_BUF_SIZE` | 64 | 输入环形缓冲区容量 |
| `LAB2_NPROC`（自定） | 8 | 静态进程槽数量 |
| `LAB2_USER_SIZE`（自定） | 64 KiB | 每个进程的用户内存，程序放在低端，栈在高端 |
| `LAB2_KSTACK_SIZE`（自定） | 8 KiB | 每个进程的内核栈 |

## 现场验收流程

任务书第 4 节“验收内容”对应以下 5 步，具体命令见 [test-plan.md](test-plan.md)：

1. 构建：`make clean && make`
2. 跑通 Shell：`make qemu` → `sh>` → `hi`、`spin`；spin 运行时敲键盘有回显
3. 官方测试：`inject_uart.py` 跑 `smoke.script`（hi + badecall）和 `bufstorm.script`，两条都要“EXPECT 失败 0 条”
4. lab1 回归：启动输出的前 3 行与 `expect_banner.txt` 一致
5. 三题问答：3 道思考题 + 设计取舍（[design.md](design.md) 第 4–7 节）

可选加分：出示带修正批注的 lab0 控制流图或中断图。

## 自测用例（任务书：动手前写好两个测试用例）

任务书举了三个例子，每个都写成了一个用户程序，内嵌进内核，由脚本通过串口自动输入并判定结果：

| 用例 | 任务书原例 | 程序 | 验证什么 |
| --- | --- | --- | --- |
| **T2-1** | 向 read 传入恰好等于缓冲区容量的长度 | `user/readcap.c` | `read(0,buf,64)` 读一行正好 64 字节的输入，返回 64；超长行不丢换行；溢出后缓冲区还能正常使用 |
| **T2-2** | 空缓冲区时 read 的行为 | `user/readempty.c` | 缓冲区为空时 read 阻塞，不返回 0 或 −1；按下一个键（不按回车）就返回，这是个人参数“字符流”的行为 |
| **T2-3** | 非法系统调用号的返回值 | `user/syserr.c` | 调用号 0、23、−1 和未实现的 `open` 都返回 −1；错误的 fd、长度、指针，以及 `exec`、`wait` 失败都返回确定的错误；之后内核仍然正常 |
| 附加 A1 | — | 脚本检查 | 所有内嵌程序的 `main` 都在地址 0（exec 的入口） |
| 附加 A2 | — | 语义 0 变体 | 在把参数改成 0 的副本上重跑 T2-1、T2-2，证明行缓冲分支也真实可用 |

运行方式：`cd labs/2024302141121-kernel && python3 tests/lab2_selftest.py`。2026-09-28 实测 13 项全部通过。

## 完成状态

- [x] `struct trapframe` 与只读 `trampoline.S` 的偏移完全一致（编译期断言 a0=112、t6=280）
- [x] U 态 ecall 进入内核、只对 ecall 推进 `sepc`、经 `sret` 返回
- [x] 系统调用分发 fork/exit/wait/read/exec/getpid/pause/uptime/write；未知调用号统一返回 −1
- [x] PLIC + UART 接收中断，64 字节环形缓冲，回显；行缓冲、字符流两种语义都能工作
- [x] M 态定时器桥接为 S 态软件中断，spin 期间仍能响应键盘
- [x] `_uprog_table` 装载 sh；fork → exec → exit → wait 闭环
- [x] 官方测试、lab1 Banner 回归、3 个自测用例全部通过
- [ ] write 往返图、键盘中断图的手绘版（design.md 第 2 节有可照着画的版本）
