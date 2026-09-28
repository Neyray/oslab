# lab1 启动与串口输出

目标：让 QEMU `virt` 从上电开始，由 M 态切到 S 态，通过轮询 UART 按学号协议打印 Banner。

| 文档 | 内容 |
| --- | --- |
| [design.md](design.md) | 设计与问答：启动主线、逐文件实现、4 道思考题、追问、故障定位 |
| [test-plan.md](test-plan.md) | 验收清单：现场命令与通过标准；两组自测用例及实测结果 |
| [`tests/lab1_selftest.py`](../../labs/2024302141121-kernel/tests/lab1_selftest.py) | 自测用例脚本，一条命令跑完 |

## 个人参数

| 参数 | 值 | 体现在哪 |
| --- | ---: | --- |
| `COURSE_SID` | 2024302141121 | Banner 第一行十进制 |
| `COURSE_SID % 97` | 35 = `0x23` | Banner 第一行十六进制 |
| `LAB1_BANNER_PROTOCOL` | 2 | 正文后追加 `[chk=12536]` |
| `LAB1_STACK_KB` | 12 | `start.c:21` 定义 `boot_stack`，`entry.S:28-30` 算栈顶 |
| 节流周期 `16 + SID % 16` | 17 | `console.c:12`，每 17 个物理字节插一段 nop |
| 节流长度 `32 + SID % 32` | 33 | `console.c:13`，每段 nop 的条数 |

## 现场验收流程

任务书第 4 节“验收要点”对应以下 5 步，具体命令见 [test-plan.md](test-plan.md)：

1. 构建：`make clean && make`
2. 格式自检（门槛）：`python3 check_expect.py 2024302141121` 必须 3 项全过，才能进入问答
3. 演示 Banner：`make qemu`，实际输出与 `expect_banner.txt` 逐字节一致
4. 三题问答：4 道思考题 + 实现细节（[design.md](design.md) 第 3–5 节）
5. 抽查 lab0 三张设计图纸（主干流转正确即可）

`lab1-submit` 指向最新提交，和 lab2 是同一棵累积内核：前 3 行（205 字节）是 lab1 Banner，后面接着 lab2 的 `lab2 ready` 和 `sh>`。如果老师要求“整段输出只有 Banner”，就用备用标签 `lab1-submit-v1`（lab1 原始提交 `0e54dad`）演示，见 test-plan.md 第 1.3 节。

## 自测用例（任务书：printf 边界 + 协议格式边界）

| 用例 | 覆盖 | 子项 |
| --- | --- | --- |
| **T1-1 printf 边界** | 任务书点名的 0、负数、空字符串、最大/最小整数，再加格式符边界 | a 0 · b INT_MIN · c INT_MAX · d 空串 · e `0xffffffff` · f 80 字节长串 · g −1 · h `%x` 的 0 · i LONG_MIN · j ULONG_MAX · k NULL 串 · l `%p` · m `%c` · n `%%` · o 未知格式 `%q` |
| **T1-2 协议格式边界** | 协议 2 的校验和与行结构，以及同一正文在协议 0 / 1 下的形态 | a 学号与 `0x23` 格式 · b 宿主机重算校验和 · c 校验和是最后一行 · d 与 expect 逐字节一致 · e 协议 0 构建 · f 协议 1 构建 |
| 附加检查 | 任务书未要求 | A1 两次冷启动输出相同 · A2 `-d int` 无同步异常 |

运行方式：`cd labs/2024302141121-kernel && python3 tests/lab1_selftest.py`。2026-09-28 实测 23 项全部通过。

## 完成状态

- [x] `entry.S`：关中断、只放行 hart 0、建 12 KiB 栈、设置 `mtvec` 兜底
- [x] `start.c`：MPP=S、`mepc=main`、委托、PMP、`satp=0`、`mret`
- [x] `console.c`：轮询 LSR.THRE 发送、个人节流、协议 2 校验和
- [x] `printf.c`：`%d %u %x %p %s %c %%` 和 `l` 修饰
- [x] Banner 与 `expect_banner.txt` 逐字节一致；两组自测用例全部通过
- [ ] lab0 三张图纸的本人手绘版（验收时抽查）
