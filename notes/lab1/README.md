# lab1 启动与串口输出

本轮在 [`../../labs/2024302141121-kernel/`](../../labs/2024302141121-kernel/) 中，从教师发放的五个空白核心文件开始实现最小 RISC-V 裸机内核。目标是完成 QEMU `virt` 上的 M 态入口、M→S 特权级切换、轮询式 UART 输出、最小格式化打印，以及学号 `2024302141121` 的协议 2 banner。

## 项目索引

- 个性化参数：[`../../labs/2024302141121-kernel/我的参数.txt`](../../labs/2024302141121-kernel/我的参数.txt)
- 设计决策：[`design.md`](design.md)
- 测试计划：[`test-plan.md`](test-plan.md)
- 实测结果：[`verification.md`](verification.md)

## 个性化参数

| 项目 | 数值 | 本轮影响 |
| --- | ---: | --- |
| `COURSE_SID` | `2024302141121` | banner 中十进制打印 |
| `LAB1_BANNER_PROTOCOL` | `2` | 正文后追加 ASCII 十进制校验和 |
| `LAB1_STACK_KB` | `12` | 唯一启动核的初始内核栈大小 |
| `COURSE_SID % 97` | `35 = 0x23` | banner 的小写十六进制字段 |
| `16 + COURSE_SID % 16` | `17` | 每输出 17 个物理字节执行一次 `nop` 节流循环 |

## 完成标准

- [x] `_entry` 从 M 态关闭中断，仅 hart 0 继续，其余 hart 停驻
- [x] `sp` 指向 12 KiB 启动栈顶，`mtvec` 指向 4 字节对齐的兜底向量
- [x] `start()` 完成 M→S 所需的 `mstatus`、`mepc`、delegation、PMP 与 Bare `satp` 配置
- [x] UART 按 LSR bit 5 轮询后写 THR，并按 17 字节周期节流
- [x] `printf` 覆盖十进制、负数、最大整数、空字符串、小写十六进制与长字符串
- [x] QEMU 输出与 `expect_banner.txt` 逐字节一致
- [x] `-d int` 日志没有同步异常（`async:0`）
- [x] 连续两次冷启动输出完全一致
