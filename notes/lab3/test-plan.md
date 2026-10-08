# lab3 指令验收清单

对应 lab3 实验说明书 V3.1 第 4 节“交付与验收”，使用增量包 v2-20260929。所有命令在 WSL 中执行，内核树为 `~/projects/oslab/labs/2024302141121-kernel`。“实测”记录 2026-10-08 的真实运行；完整断言输出见 [test-results.txt](test-results.txt)。

| 任务书要求 | 本文位置 |
| --- | --- |
| 干净构建、正常进入 Shell | 1.1–1.2 |
| lab1 Banner 与 lab2 UART 回归 | 1.3 |
| dump 输出、权限位与 lab0 快照对应 | 1.4 |
| 官方 badaccess / memcap | 1.5 |
| spin 与 sh 并发稳定运行 | 1.6 |
| 自写保护页异常、跨页拷贝测试 | 2.1–2.2 |
| 映射、增长、fork/exec 失败回滚与配额 | 2.3–2.4 |
| 日志检查、预置文件、提交标签与归档 | 1.7–1.8、3 |

## 1 现场验收

### 1.1 验收前状态核对

~~~bash
cd ~/projects/oslab
git status --short
git log -1 --oneline lab3-start
git log -1 --oneline lab3-submit
cd ~/projects/oslab/labs/2024302141121-kernel
command -v riscv64-unknown-elf-gcc
command -v qemu-system-riscv64
~~~

通过标准：工作区干净；`lab3-start` 是增量包导入提交，`lab3-submit` 是最终实现；课程编译器与 QEMU 可用。**实测：增量包单独提交 `a2570d0`，已安装课程 unknown-elf 工具链，最终提交与标签在全部验证完成后创建。** 标签核对记录见本文第 3 节。

### 1.2 干净构建与启动

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean && make -j4
make qemu
~~~

通过标准：构建退出码 0，`-Wall -Werror` 下无编译警告；生成 15 个用户程序；启动打印原 Banner、完整 dump 块、`lab3 ready`、`lab2 ready`，最后出现 `sh>`。交互结束按 `Ctrl-a`，松开后按 `x`。

**实测：通过。** 课程 unknown-elf 工具链构建无警告，默认 sh 映射是代码 0x0、数据 0x1000、guard 0x2000/0x3000、栈 0x4000，初始堆边界 0x5000。构建器仍支持 `TOOLPREFIX` 覆盖，优先使用课程工具链；本轮最终实测使用课程工具链。

### 1.3 先跑早期实验回归

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab1_selftest.py
python3 tests/lab2_selftest.py
python3 support/inject_uart.py --tree . --script support/tests/smoke.script --log /tmp/lab3-smoke.log
python3 support/inject_uart.py --tree . --script support/tests/bufstorm.script --log /tmp/lab3-bufstorm.log
~~~

| 检查 | 通过标准 | 实测 |
| --- | --- | --- |
| lab1 自测 | 所有 printf / 协议 / 冷启动 / 正常异常日志断言通过 | `23 passed, 0 failed` |
| lab2 自测 | 两种缓冲语义、读边界、非法调用与错误指针通过 | `13 passed, 0 failed` |
| smoke | hi + badecall，EXPECT 失败 0 条 | 通过 |
| bufstorm | `BUFSTORM lines=4`，EXPECT 失败 0 条 | 通过；有效字节数随输入与读取时序变化 |

Banner 还可手工逐字节检查：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 check_expect.py 2024302141121
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic > /tmp/lab3-banner.txt 2>/dev/null
head -n 3 /tmp/lab3-banner.txt | cmp - expect_banner.txt && echo BANNER_OK
~~~

timeout 的退出码 124 是主动结束裸机模拟器，属于预期。通过标准：形状校验通过，前三行逐字节相同，校验和仍为 12536。**实测：自动脚本逐字节比较通过，Banner 为 205 字节；check_expect.py 与上述手工命令亦通过。** 只比较前三行，后续 dump / Shell 是累积内核新增输出。

lab2 的 A1 现在核对“ELF entry 等于 main”，不再固定 main=0；因为 lab3 把 ELF 入口传到装载器，允许辅助函数位于 main 之前。T2 的输入输出与错误语义断言保持原要求。

### 1.4 dump 与权限现场演示

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make qemu
~~~

启动后自动打印一次；在 `sh>` 等待输入时按 **Ctrl-P**，可重放当前进程页表与空闲页统计。自动验收入口：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --skip-mm
~~~

| 行 / 区域 | 判据 | 实测 |
| --- | --- | --- |
| 块边界 | 行首 `DUMP-PAGETABLE begin/end` | 通过，包括 Ctrl-P 重放 |
| L2 / L1 | 每个非叶表项输出一行，perm=---- | 通过 |
| 用户代码 | `LEAF va=0x0 ... perm=r-xu` | 通过 |
| 数据 / 栈 | `rw-u` | 通过 |
| 两页 guard | 没有对应有效叶映射 | 通过 |
| trapframe | `va=0x3fffffe000 ... perm=rw--` | 通过 |
| trampoline | `va=0x3ffffff000 ... perm=r-x-` | 通过 |

A/D 不放进 perm：本实现软件预置 A，W 页预置 D。应翻出 lab0 图纸，注明当前 guard 不分配物理页、内核栈采用静态恒等映射，与参考树快照的不同。图纸本人复核与手绘为**待本人完成**，没有用自动测试冒充已完成。

### 1.5 官方用户态测试

手工在 sh 中逐项输入：

~~~text
badaccess
memcap
~~~

自动化与日志：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --all-orders
cat /tmp/lab3-user.log
~~~

| 官方项目 | 通过标准 | 实测 |
| --- | --- | --- |
| TEST-3a | 写代码页被终止，父 wait 得到非零状态 | PASS，status=-1，cause 15 |
| TEST-3b | 申请一页后越界读被终止 | PASS，status=-1，cause 13 |
| TEST-4 | 超配额返回错误，旧内存保持可用，Shell 继续工作 | PASS，2036 个 64 KiB 块后 OOM |

OOM 后应重新出现 `sh>`，再执行 hi 仍能正常运行。2036 是本次镜像的快照值，随着内核静态大小改变可能变化；合格判据是达到配额返回错误、原数据可用、系统稳定。

### 1.6 预置 spin 与 sh 并发

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make qemu
~~~

在 sh 中输入 `concurrent`；出现新的 `sh>` 与持续的 `spin N` 后，再输入 `hi`。测试结束用 Ctrl-a x 退出，spin 本来就是无限循环。也可查看自动记录：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --skip-mm
cat /tmp/lab3-concurrent.log
~~~

通过标准：spin 在 sh 等待输入时继续输出；hi 输出正常；重新出现 sh 提示符，spin 继续递增。**实测：通过。** 两个预置用户程序均未修改；concurrent 只是启动一个 spin，再把自己 exec 为 sh。另有 schedtest 验证子进程在父 wait 之前获得执行机会，以及 pause 到期后恢复。

### 1.7 正常路径与故意错误路径的异常日志

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic -d int -D /tmp/lab3-normal-int.log \
  > /tmp/lab3-normal-console.log 2>&1
grep 'async:0' /tmp/lab3-normal-int.log | grep -vc 'cause:0000000000000008'
~~~

通过标准：正常启动没有 ecall 以外的同步异常；自动脚本还注入 hi/badecall 检查。**实测：0 条异常同步 trap。** `grep -c` 在计数为零时退出码为 1，不代表发生了一条异常。

badaccess / guardtest 的 cause 13/15 是预期的用户错误，判据是该进程被终止、父进程收到非零状态并且系统仍运行。对这类用例不能要求“异常数为零”。本机时钟在 S 态记录为软件中断 1，仍沿用 lab2 的 M 态 CLINT 桥接，不是 Sstc 中断 5。

### 1.8 预置文件与用户指针审查

~~~bash
cd ~/projects/oslab
cmp materials/lab3-v2-20260929/gifts/kernel/trampoline.S labs/2024302141121-kernel/kernel/trampoline.S
cmp materials/lab3-v2-20260929/gifts/kernel/riscv.h labs/2024302141121-kernel/kernel/riscv.h
git diff --exit-code lab3-start -- \
  labs/2024302141121-kernel/kernel/course_sid.h \
  labs/2024302141121-kernel/kernel/kernel.ld \
  labs/2024302141121-kernel/kernel/memlayout.h \
  labs/2024302141121-kernel/kernel/types.h \
  labs/2024302141121-kernel/kernel/entry.S \
  labs/2024302141121-kernel/kernel/start.c \
  labs/2024302141121-kernel/kernel/timervec.S \
  labs/2024302141121-kernel/kernel/param.h \
  labs/2024302141121-kernel/kernel/syscall.h \
  labs/2024302141121-kernel/kernel/vm.h \
  labs/2024302141121-kernel/user/sh.c \
  labs/2024302141121-kernel/user/hi.c \
  labs/2024302141121-kernel/user/spin.c \
  labs/2024302141121-kernel/user/user.ld \
  labs/2024302141121-kernel/user/user.h \
  labs/2024302141121-kernel/user/ulib.c \
  labs/2024302141121-kernel/user/printf.c \
  labs/2024302141121-kernel/user/usys.pl
git diff --check
cd ~/projects/oslab/labs/2024302141121-kernel
grep -nE 'argraw|proc_copy|proc_validate|copyout' kernel/syscall.c kernel/proc.c
~~~

通过标准：cmp、diff 与空白检查无输出且退出码 0；逐个核对所有用户指针入口，不能只凭 grep “没搜到”证明安全。**实测：通过。** sys_write/read/exec/wait 的指针均走受控校验和拷贝；其余参数仅为整数。装载与 fork 中的 memory_copy 源/目标是内核掌握的 PA，不是用户直接提交的 VA。官方测试源码也与增量包逐字节一致。

## 2 自测用例

任务书要求编码前设计两组自测：“保护页异常安全终止进程”“跨越物理页边界的受控拷贝”。[test-design.md](test-design.md) 在核心实现前建立，随后扩充 OOM 和映射事务用例；以下脚本统一运行：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --all-orders
~~~

**汇总实测：`25 passed, 0 failed`。** 副本参数测试只修改 /tmp 临时树，生产 course_sid.h 原件不变。默认参数下用户测试前后空闲页 `32650 → 32650`。

### 2.1 T3-1：跨页、权限与堆边界（user/vmtest.c）

| 输入 / 动作 | 预期不变式 | 失败表现 | 实测 |
| --- | --- | --- | --- |
| sbrk 两页，从页尾前 8 字节开始 write | 输出 `CROSS-PAGE-OK!` 完整，不依赖 PA 连续 | 后半字符串错误或内核异常 | PASS |
| 出现 `VMTEST input-ready` 后输入 `cross-input` 回车 | 跨页 read 返回 12 字节，内容逐字节一致 | 部分页写坏、长度不符 | PASS |
| read 目标设为只读代码页 | 立即 -1，不等待键盘输入 | 卡在读取或写代码成功 | PASS |
| 高地址、跨 sz、溢出指针 | 全部 -1，内核保持运行 | 泄露、异常或部分错误输出 | PASS |
| +1、+1、−2 与 LAZY 请求 | 字节级边界正确；LAZY 暂按 EAGER | 错误释放仍使用的页、边界错位 | PASS |
| 父数据=11，子改22 | 父仍为11，wait 正常 | 数据共享污染 | PASS |
| wait 只读指针后再合法 wait | 非法指针不消费子进程；合法指针得到9 | 子进程丢失、状态未写回 | PASS |
| pause 2 tick | 不早于2 tick恢复，返回0 | 参数被覆盖、提前返回、死等 | PASS |

手工操作：sh 输入 vmtest，出现 input-ready 后输入 `cross-input` 回车；看到 `VMTEST PASS` 后重新出现 Shell。

### 2.2 T3-2：两页 guard 的读写异常（user/guardtest.c）

从当前用户 sp 推导栈页底端，分别访问下方第 1 / 第 2 页；每页各读一次、写一次，动作发生在 fork 的子进程中。

预期：4 个子进程均以非零状态退出，父进程 wait 回收后继续，最终 `GUARDTEST PASS pages=2 failures=0`。错误实现可能使子进程存活、把 guard 当普通数据页，或让整个内核停住。

**实测：PASS。** 读 cause 13、写 cause 15；父进程与 sh 均正常。手工在 sh 输入 guardtest 即可，无额外输入。

### 2.3 T3-3：内核物理页与映射事务（kernel/mmtest.c）

测试仅在 `LAB3_MM_SELFTEST` 条件编译副本中执行，正式构建不带测试数组与测试输出。自动脚本分别对 order 0/1/2 建立临时副本后，运行：

| 场景 | 预期不变式 | 实测 |
| --- | --- | --- |
| 按独立排序数组逐次选择页 | 升序/降序/下中位数正确、4 KiB 对齐 | 三种顺序均 PASS |
| 分配、填值、释放、再耗尽 | 返回页全清零，耗尽 NULL，最终计数守恒 | PASS |
| 重复映射 VA 0 | 返回 -1，旧 PTE 与计数不变 | PASS |
| 留2页，跨1 GiB页表分支映射 | 中途失败后本次叶子、新表全撤销，旧空表仍存在 | PASS |
| 留1页，walk 新分支 | 第二层分配失败，第一层也回收 | PASS |
| 留2页，增长多个数据页 | 失败后旧映射、旧数据、空闲计数恢复 | PASS |
| 跨两张不连续物理页 copyinstr/copyin | 字符串与字节完全正确 | PASS |
| copyout 跨只读页边界 | 预检失败，不先写另一页 | PASS |
| 全部释放 | 回到初始空闲页数，输出 `MMTEST RESULT failures=0` | PASS |

首轮测试曾暴露“增长回滚误删旧空表”，已修复，详见 design.md 4.2。当前结果来自修复后的真实执行。

### 2.4 T3-4：OOM、部分 fork/exec 失败与恢复（user/oomtest.c）

先用 64 KiB，再用 4 KiB 请求把可分配页耗尽；检查 fork/exec 返回 -1，原数据不变。释放仅 5 页后 exec hi，迫使新地址空间在部分建立之后失败；检查这 5 页还能再次原样申请。再释放 7 页后尝试 fork，验证部分深拷贝失败的清理，随后整段归还，再 fork/wait 验证恢复。

通过标准：`OOMTEST PASS`，堆边界、原数据与资源计数均保持一致。**实测：PASS，增长总字节为 133443584，退出回到 Shell；所有用户测试后 free 与测试前相同。** 字节数同样是当前镜像快照，不是硬编码的验收上限。

### 2.5 T3-5：并发与唤醒（user/schedtest.c / concurrent.c）

schedtest 的子进程 pause 后输出 `SCHED-CHILD`，父进程随后输出 `SCHED-PARENT-WAIT` 并 wait 得到状态6；自动脚本检查两行顺序，证明子进程不是只等父 wait 才运行。concurrent 则直接复用预置 spin/sh，覆盖读输入让出执行机会与 UART 唤醒。**两项均实测 PASS。**

## 3 提交、标签与归档

### 3.1 版本边界

本次按明确要求，先单独提交增量包，再修改实现：

| 引用 | 内容 | 状态 |
| --- | --- | --- |
| `lab3-start` = `a2570d0` | 课程 v2 原始预置修复、官方测试、任务书与接口规范 | 已提交并标记 |
| `lab3-submit` | 最终实现、设计问答、验收清单与真实测试输出 | 全部测试通过后提交并标记 |
| 远端 | 本轮没有执行 push | 未推送 |

核对命令：

~~~bash
cd ~/projects/oslab
git status --short
git log -2 --oneline
git show --no-patch --format='%h %s' lab3-start
git show --no-patch --format='%h %s' lab3-submit
test "$(git rev-parse HEAD)" = "$(git rev-parse lab3-submit)" && echo TAG_OK
~~~

通过标准：工作区干净，TAG_OK，两个提交清楚区分课程导入与实验实现。最终提交号以实际 git 输出为准，不在文档中嵌入会形成自引用的实现提交哈希。

### 3.2 归档（待本人按需执行）

本轮用户要求的是 commit 与 tag，尚未生成提交 zip，也没有把 CRC 记为已通过。需要交作业时执行：

~~~bash
cd ~/projects/oslab
git archive --format=zip -o /tmp/提交-lab3-2024302141121.zip lab3-submit
python3 -c 'import zipfile; p="/tmp/提交-lab3-2024302141121.zip"; z=zipfile.ZipFile(p); assert z.testzip() is None; print("ZIP_CRC_OK", len(z.namelist()))'
~~~

通过标准：ZIP_CRC_OK，归档包含代码、官方规范与 notes/lab3。**当前实测：待执行。** 现场手绘图与本人问答准备亦不由 git 标签代替。
