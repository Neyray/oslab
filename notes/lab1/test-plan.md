# lab1 指令验收清单

对应任务书第 4 节“提交与验收要求”：

| 任务书要求 | 本文位置 |
| --- | --- |
| 格式自检：`check_expect.py` 3 项全过（进入问答的门槛） | 1.2 |
| 现场演示 Banner，内核输出与 `expect_banner.txt` 逐字节比对 | 1.3 |
| 自测用例：printf 边界 + 协议格式边界，保存在项目中备查 | 2 |
| 版本归档：`lab1-submit` 标签 + zip | 3 |

所有命令在 WSL 中执行。“实测”一列记录 2026-09-28 在当前提交上的真实运行结果。

## 1 现场验收

### 1.1 检出 lab1 版本并构建

`lab1-submit` 的内核只打印 Banner。main 上是 lab1–lab2 的累积内核，所以先用 worktree 单独检出 lab1 版本（不影响主工作区）：

~~~bash
cd ~/projects/oslab
git worktree add /tmp/lab1 lab1-submit
cd /tmp/lab1/labs/2024302141121-kernel
make clean && make
~~~

验收结束后执行 `cd ~/projects/oslab && git worktree remove /tmp/lab1`。

通过标准：退出码 0；`-Wall -Werror` 下没有任何 warning。**实测：通过。**

### 1.2 格式自检（门槛）

~~~bash
python3 check_expect.py 2024302141121
~~~

通过标准：输出 `[ok] 形状校验通过(sid=2024302141121, 协议2)`。脚本检查 3 项：包含学号十进制；包含 `0x23`（小写、无前导零）；协议 2 的最后一段是 ASCII 数字校验和，且后面没有其他正文。**实测：通过。**

### 1.3 Banner 演示与逐字节比对

~~~bash
make qemu                      # 看完按 Ctrl-a 松开再按 x 退出

timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic > /tmp/l1.txt 2>/dev/null
cmp /tmp/l1.txt expect_banner.txt && echo FULL_OK
~~~

期望的整段输出（205 字节）：

~~~text
OSLAB1 sid=2024302141121 mod97=0x23
selftest zero=0 neg=-2147483648 max=2147483647 empty='' hex=0xffffffff long=01234567890123456789012345678901234567890123456789012345678901234567890123456789
[chk=12536]
~~~

**实测：`FULL_OK`。** 之后 lab2 的累积内核会在 Banner 后继续输出，lab2 的回归测试只比较前 3 行（`head -n 3`）。

### 1.4 源码核查（问答时可能要求指出）

~~~bash
grep -n 'THROTTLE_PERIOD\|THROTTLE_NOP_COUNT' kernel/console.c
grep -n 'LAB1_STACK_KB' kernel/start.c kernel/entry.S
~~~

通过标准：节流周期写成 `16UL + (COURSE_SID % 16UL)`，没有写死 17；栈大小的两处都引用 `LAB1_STACK_KB`。**实测：通过。**

## 2 自测用例

任务书原文：“编写针对 printf 的边界测试用例（如 0、负数、空字符串、最大/最小整数等），以及协议格式边界用例，保存在项目中备查。”

两组用例由 [`tests/lab1_selftest.py`](../../labs/2024302141121-kernel/tests/lab1_selftest.py) 一次跑完：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab1_selftest.py
~~~

脚本流程：构建 → 启动 QEMU 抓取串口输出 → 逐项断言，最后打印 `== N passed, M failed`。需要换参数的用例（协议 0/1、额外 printf）会把代码树复制到 `/tmp` 再修改副本，**不会改动预置的 `course_sid.h`**。

### 用例 T1-1：printf 边界

a–f 直接取自 Banner 第二行 `selftest ...`，它随每次启动打印，所以现场 `make qemu` 就能看到。g–o 来自测试构建（`-DLAB1_PRINTF_EXTRA_TEST`）额外打印的一行，默认内核不编译这一行。

| 子项 | 调用 | 边界 | 预期输出 | 实测 |
| --- | --- | --- | --- | --- |
| a | `%d`, 0 | 零只输出一位 | `0` | PASS |
| b | `%d`, INT_MIN | 负数；取负会溢出 | `-2147483648` | PASS |
| c | `%d`, INT_MAX | 最大整数 | `2147483647` | PASS |
| d | `%s`, `""` | 空字符串 | `''`（引号之间为空） | PASS |
| e | `%x`, 0xffffffff | 全 1；小写、带 `0x`、无前导零 | `0xffffffff` | PASS |
| f | `%s`, 80 字符 | 长字符串不截断 | 80 位数字 | PASS |
| g | `%d`, −1 | 最小的负数绝对值 | `-1` | PASS |
| h | `%x`, 0 | 零也要至少一位 | `0x0` | PASS |
| i | `%ld`, LONG_MIN | 64 位最小值 | `-9223372036854775808` | PASS |
| j | `%lu`, ULONG_MAX | 64 位无符号最大值 | `18446744073709551615` | PASS |
| k | `%s`, NULL | 空指针不崩溃 | `(null)` | PASS |
| l | `%p`, 0x80000000 | 指针格式 | `0x80000000` | PASS |
| m | `%c`, `'Z'` | 单字符 | `Z` | PASS |
| n | `%%` | 转义百分号 | `%` | PASS |
| o | `%q` | 未知格式不吞掉 | `%q` | PASS |

失败时的表现：b 若直接 `-value` 会得到乱码或 `-0`；e/h 若补了前导零或少了 `0x`，`check_expect.py` 也会失败；k 若不判空，会从物理地址 0 读取，结果不可预期（访问异常，或打印出一串乱码）。

### 用例 T1-2：协议格式边界

| 子项 | 检查内容 | 预期 | 实测 |
| --- | --- | --- | --- |
| a | 第 1 行格式 | `OSLAB1 sid=2024302141121 mod97=0x23`，学号十进制、`0x` 小写无前导零 | PASS |
| b | 校验和正确 | 宿主机对前两行（含换行）逐字节求和 = 内核输出的值 | PASS（12536 = 12536） |
| c | 校验和的位置 | `[chk=12536]` 是 Banner 的第 3 行，也是最后一行，以 `\n` 结尾 | PASS |
| d | 逐字节一致 | 前 205 字节与 `expect_banner.txt` 完全相同 | PASS |
| e | 协议 0 构建 | 副本中 `LAB1_BANNER_PROTOCOL=0`：正文不变，后面没有 `[chk=` 行 | PASS |
| f | 协议 1 构建 | 副本中 `LAB1_BANNER_PROTOCOL=1`：每个字节后都跟 `.`，包括换行符（`\n.`） | PASS（386 字节） |

e、f 验证的是 `console_putc` 的 `#if LAB1_BANNER_PROTOCOL` 分支。这两个分支我自己的参数用不到，但它们是同一套协议代码的边界：换行符后面也要跟 `.`；协议 0 不能残留校验和行。

### 附加检查（任务书未要求）

| 编号 | 内容 | 实测 |
| --- | --- | --- |
| A1 | 两次冷启动输出逐字节相同（没有未初始化变量影响输出） | PASS |
| A2 | `-d int` 日志中除 ecall（cause 8）外没有同步异常 | PASS（0 条） |

**汇总实测：`== 23 passed, 0 failed`。**

## 3 提交与归档

~~~bash
cd ~/projects/oslab
git status --short
git log -1 --oneline lab1-submit
git archive --format=zip -o 提交-lab1-2024302141121.zip lab1-submit
~~~

`lab1-submit` 是在 lab1 原始提交 `0e54dad` 之上补入本轮笔记和自测用例的提交，不在 main 的直线历史上，因为 main 上的内核已经包含 lab2。两个标签的分工见 [README.md](README.md#两个标签的分工)。

归档包放在 Windows 侧 `OS实践/lab1/提交-lab1-2024302141121.zip`。
