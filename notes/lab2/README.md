# lab2 陷入 系统调用与中断控制台

本轮把 Lab1 的单向串口输出扩展为可运行用户程序的最小单核内核。当前实现能够进入 `sh>`，执行 `hi`、`badecall`、`bufstorm`，并在 `spin` 忙循环期间继续响应时钟与 UART 中断。

## 项目索引

- 个性化参数：[`../../labs/2024302141121-kernel/我的参数.txt`](../../labs/2024302141121-kernel/我的参数.txt)
- 设计与验收问答：[`design.md`](design.md)
- 测试与回归记录：[`test-plan.md`](test-plan.md)
- 官方测试入口：[`../../labs/2024302141121-kernel/support/tests/`](../../labs/2024302141121-kernel/support/tests/)

## 个性化参数

| 参数 | 数值 | 实现影响 |
| --- | ---: | --- |
| `LAB2_TICK` | 3 | M 态定时器间隔为 `100000 × 3` 个 timebase 单位 |
| `LAB2_BUF_SEMANTICS` | 1 | 单字节读取收到字符即可返回，多字节读取保留行边界 |
| `LAB2_BUF_SIZE` | 64 | UART 接收环形缓冲区容量为 64 字节 |

## 完成标准

- [x] `trapframe` 字段偏移与只读 `trampoline.S` 完全一致
- [x] U 态 ecall 能进入内核、推进 `sepc` 并经 `sret` 返回
- [x] 未知系统调用统一返回 `-1`
- [x] `fork`、`exec`、`wait`、`exit` 支撑 Shell 子进程闭环
- [x] PLIC 与 UART 接收中断接通，输入可回显并进入环形缓冲区
- [x] M 态定时器通过 S 态软件中断桥接，`spin` 期间持续响应中断
- [x] `hi`、`badecall`、`bufstorm` 和 Lab1 Banner 回归通过

