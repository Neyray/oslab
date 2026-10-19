# lab4 指令验收清单

以实验说明书 V3 第 4 节的“官方机制测试 → 个人策略观测 → 历史回归 → 三题问答 → 标签归档”为主线。下面记录 2026-10-19 在 WSL2 Ubuntu-22.04、QEMU virt 单核的真实结果。动态 PID、time 计数和空闲页数量不作为跨版本固定判据。

## 1 验收前状态核对

```bash
cd /home/jerico/projects/oslab
git status --short --branch
git log -2 --oneline lab4
git show --no-patch --oneline lab4-start
git show --no-patch --oneline lab4-submit
```

通过标准：位于 lab4 分支，工作树干净；相对原 main 恰好两个提交；lab4-start 指向第一轮增量导入，lab4-submit 指向第二轮完整实现。当前为本地提交与标签，未推送 GitHub；main 仍为 lab3，不能误把 main 的旧代码当作本轮成果。

第一轮实测：3fd9c1e 合入 v2 原包、V3 任务书和构建接入，尚未重构内核；当时 lab3 自测 --skip-mm 为 22/22，见 [导入回归](evidence/lab4-import-regression.txt)。

## 2 干净构建与启动

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
make clean
make > /tmp/lab4-build.log 2>&1
make qemu
```

通过标准：-Wall -Werror 编译链接成功，前三行与 expect_banner.txt 一致，随后出现 DUMP-PAGETABLE begin/end、lab3 ready、lab2 ready 和 sh>。实测通过。make qemu 的课程预置调用默认单核；本轮不把 CPUS=2 变量当作双核验证。

交互退出：Ctrl-a 松开后按 x。下列 Python 测试自行启动并终止 QEMU，含分步超时；外层 timeout 到时也应检查是否有遗留 QEMU 进程。

## 3 官方 bench

从 Shell 输入：

```text
bench
```

通过标准：输出 RESULT avg_fork_wait = <整数>、RESULT concurrent_children = 8、PASS: bench，并回到 sh>。实际 v2 源码为 100 轮 fork/exit/wait 与 8 个并发孩子，不能按说明书残留的 16 轮叙述判断。

当前实测连续三次通过；平均往返为 9467、8815、9080 个 time 计数，属于当次性能快照，不是固定阈值。官方原程序和 common 库未修改。见 [UART 证据](evidence/lab4-user.txt)。

## 4 个人三级 MLFQ 观测

Shell 输入：

```text
mlfqtest
```

通过标准：RESULT mlfq_slices = 7（位掩码 1|2|4，表示三档均观测到）、三个计算进程的 CPU 增量均推进、最大差不超过两个最低级时间片；输出交互最大唤醒间隔、MLFQTEST PASS 并回到 sh>。

用例 T4-1：三个纯计算子进程，父进程每次 pause(1) 后 schedstat；先预热 16 次采样，再在统一 48 次采样窗口计算差分。当前实测 64:64:64。失败表现：缺某档、片长大于 4、某子不推进或同级长期失衡。

用例 T4-2：先让一个计算进程降到最低级，再 fork 出短计算+pause(1) 的交互孩子。交互孩子逐次检查 slice<=1、pause 实际经过至少一拍且至多五拍；父程序确认后台在交互期间仍推进。当前最大唤醒间隔 4 tick。

输出 MLFQ interactive-ready pid=<pid> 时按 Ctrl-P，可见指定 PID 的 level=0；后台 level=2。自动脚本按实际 PID 匹配，避免把父程序同名行误认为交互孩子。采样正则等待行尾，避免把 64 的首位 6 当作完整统计。

## 5 边界、错误与恢复

Shell 输入：

```text
schedabi
alt
```

T4-3 通过标准：SCHEDABI PASS；包括 schedstat 的自身、其他存活进程、未知/负数/已退出 PID、代码页只读指针、溢出指针、跨页输出；setickets 0/-1 失败、1/INT_MAX 成功；pause(-1)/pause(0)/pause(3)；kill 睡眠目标、wait 错误指针后仍能回收、32 轮槽位复用。另把进程表填满：shell+本程序之外应创建 62 个孩子；下一次 fork 返回 -1；清理后再次 fork 成功。当前全部通过，RESULT process_capacity = 62。

T4-4 通过标准：alt 输出恰好 16 个 A 和 16 个 B，至少三段交替连续块，最后 alt done 并回到 Shell；不能仅输出一整块 A 再一整块 B。当前实测如 AAAAAAAAAAABBBBBBBBBBBBBBBBAAAAA，通过。两个子程序没有主动 pause，因此能证明时钟抢占。

完整自动入口：

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
timeout 180s python3 -u tests/lab4_selftest.py --experiments > /tmp/lab4-selftest.log 2>&1
cat /tmp/lab4-selftest.log
```

实测 45 passed, 0 failed，包括官方 bench 三次、MLFQ、ABI、抢占、页数守恒、预置文件以及下一节两种临时实验。证据：[完整摘要](evidence/lab4-selftest.txt)、[用户输出](evidence/lab4-user.txt)。

## 6 轮转机制与丢失唤醒复现

执行上一节的 --experiments，脚本将临时复制代码树；正式树与个性化宏不受影响。

轮转副本用 LAB4_RR_TEST 编译，忽略队列级别、每次用户态时钟均让出；bench 和 alt 通过。见 [RR 证据](evidence/lab4-rr.txt)。

错误锁序副本在 wait 条件锁释放与设置 SLEEPING 之间空转并 proc_yield，控制子进程先 exit。通过标准是该故障确实复现：两秒观察窗口内没有 bench RESULT/通过行，Ctrl-P 同时显示父 SLEEPING 和子 ZOMBIE。这里的“通过”表示成功复现故障，不表示错误内核功能正常。实测见 [丢失唤醒证据](evidence/lab4-lost-wakeup.txt)。

为什么要加受控 yield：本实现只在用户态时钟抢占，单纯内核空转不能让另一个进程运行；不能把未复现的纯延迟方案写为实测。正式树恢复正确“先 p.lock 再释放条件锁”顺序后，bench 连续三次成功。详细推演见 [设计问答](design.md#任务书思考题)。

修复公平性前曾出现一个子进程占满窗口，见 [失败证据](evidence/lab4-fairness-before.txt)；该证据有意保留 FAIL，用于解释每级独立游标的必要性。

## 7 早期实验回归

### lab1 Banner 与 printf

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
timeout 180s python3 -u tests/lab1_selftest.py > /tmp/lab4-lab1-regression.log 2>&1
cat /tmp/lab4-lab1-regression.log
```

通过标准及实测：23/23、0 失败，包含 Banner 逐字节、边界格式、不同协议临时构建、两次冷启动及引导同步异常。见 [lab1 摘要](evidence/lab4-lab1-regression.txt)。

### lab2 两种缓冲语义及错误系统调用

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
timeout 180s python3 -u tests/lab2_selftest.py > /tmp/lab4-lab2-regression.log 2>&1
cat /tmp/lab4-lab2-regression.log
```

通过标准及实测：13/13、0 失败，readcap/readempty/syserr、另一缓冲语义临时副本都通过。23 号已成为合法 schedstat，未知调用测试改为 127；没有删去非法调用断言。见 [lab2 摘要](evidence/lab4-lab2-regression.txt)。

任务书指定的 UART 回归亦可直接运行：

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
python3 support/inject_uart.py --tree . --script support/tests/smoke.script --log /tmp/lab4-smoke.log
python3 support/inject_uart.py --tree . --script support/tests/bufstorm.script --log /tmp/lab4-bufstorm.log
```

两条命令已由 lab3 自测实际调用验证，均 EXPECT 失败 0 条。smoke 覆盖 hi/badecall，bufstorm 输出 BUFSTORM lines=4。

### lab3 页表、保护、OOM 与并发

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
timeout 240s python3 -u tests/lab3_selftest.py --all-orders > /tmp/lab4-lab3-regression.log 2>&1
cat /tmp/lab4-lab3-regression.log
```

通过标准及实测：25/25、0 失败，badaccess、guardtest、vmtest 跨页、schedtest、memcap、oomtest、spin/sh、UART 与分配顺序 0/1/2 临时内核测试。页表输出行逐条匹配规定 ABI；guard 没有映射；trapframe/trampoline 无 U；用户代码 RX、数据/栈 RW。memcap 实测 2028 个 64 KiB 块后返回 OOM；用户测试前后空闲页为 32527→32527。见 [lab3 摘要](evidence/lab4-lab3-regression.txt)。

旧 schedtest 增大 uptime 查询之间的纯计算段，以覆盖用户态时钟抢占；仍检查 SCHED-CHILD 在 SCHED-PARENT-WAIT 之前出现，未削弱通过条件。

## 8 Ctrl-P、异常日志与资源守恒

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
make qemu
```

在 Shell 空闲和 mlfqtest 交互阶段分别按 Ctrl-P。通过标准：原 DUMP-PAGETABLE begin/end 不变；新增 PID PPID STATE LEVEL CPU SLICE TICKETS NAME；空闲 current 为空时能输出 initproc 页表。自动自测已实跑：空闲 sh 为 SLEEPING；交互孩子为 level 0、后台为 level 2；最后只剩 sh，页数恢复 32527。

异常日志使用课程无 gdb 工作流，正常 hi/badecall 的自动入口为第 7 节 lab3 脚本：它带 -d int -D 临时日志运行 QEMU，先排除正常 cause=8 ecall，再统计其他 async:0。实测 0 个意外同步异常。badaccess/guardtest 的 cause=15 等为预期错误测试，不能混入这项正常会话统计。

需要手工采集故障时：

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
qemu-system-riscv64 -machine virt -bios none -smp 1 -kernel kernel/kernel -nographic -d int -D /tmp/lab4-int.log
```

复现后 Ctrl-a、x 退出，再执行：

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
rg 'async:0' /tmp/lab4-int.log
riscv64-unknown-elf-objdump -d kernel/kernel > /tmp/lab4-kernel.asm
```

根据实际 epc/stval 用 riscv64-unknown-elf-addr2line -e kernel/kernel <地址> 查询；地址必须来自故障日志，不填猜测值。本节手工故障采集是排障入口，不另记为已复现的新错误。

## 9 预置文件完整性

```bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
python3 - <<'PY'
import sys
sys.path.insert(0,'tests')
from harness import Report
from lab4_selftest import integrity
r=Report('preset integrity')
integrity(r)
raise SystemExit(r.finish())
PY
```

通过标准：lab4 gifts 的 swtch.S/syscall.h/user.h/usys.pl 与原包逐字节一致，官方 support/tests 与 user 拷贝一致；历史预置 kernel.ld/riscv.h/types.h/memlayout.h/course_sid.h/trampoline.S、用户链接脚本/库/sh/hi/spin 与 lab3-submit 的 SHA256 一致。实测全部通过。

历史哈希已保存 tests/lab4-presets.json，故归档解压后没有 .git 也能运行完整性校验；无需在验收包中重建仓库。

## 10 提交、标签与归档

本轮由用户明确授权本地 Git 操作，使用两个提交，不改旧标签。第一轮仅导入，第二轮包含实现、测试与验收笔记。归档命令：

```bash
cd /home/jerico/projects/oslab
git archive --format=zip --output='/mnt/d/BaiduSyncdisk/大学学习文件/大三上/OS实践/lab4/提交-lab4-2024302141121.zip' lab4-submit
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile
p=Path('/mnt/d/BaiduSyncdisk/大学学习文件/大三上/OS实践/lab4/提交-lab4-2024302141121.zip')
with ZipFile(p) as z:
    bad=z.testzip()
    assert bad is None, bad
    print('CRC PASS',len(z.namelist()),'entries')
PY
git status --short --branch
```

交付时核对：lab4-start 为第一轮，lab4-submit 为第二轮；工作树干净；ZIP CRC 通过。最终执行结果以本轮聊天交付信息为准。未执行 git push，远端没有本轮分支与标签的已验证更新。
