# 学生调试手册 · 内核无 GDB 核心调试方法

> 整理自 `07-学生无gdb调试手册.docx`。原始文档见 [`materials/07-学生无gdb调试手册.docx`](../materials/07-学生无gdb调试手册.docx)。
>
> **这是全学期最常翻的一份文档。**

- **什么时候用**：黑屏、卡死、panic、数据异常——任何时候内核运行异常、行为与预期不符。
- **为什么没有 gdb**：多数发行版的 RISC-V 工具链不带 gdb。本课程统一用 **QEMU 自带的调试能力 + 你自己内核里的观测设施**——对自研内核这比 gdb 更好用，因为你看得见自己写的每一行。

---

## 〇、三个常识

**1. 退出 QEMU**：按 `Ctrl-a`，松开，再按 `x`。

**2. 实验命令模板**：评分测试与故障复现都在 `kernel/kernel` 上直接跑 QEMU。想加调试参数就**别用 `make qemu`**：

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
      -d int -D int.log &
QPID=$!; sleep 5; kill $QPID     # 等待 5 秒后终止 QEMU 进程
```

串口输出直接打在终端；`-D` 指定的日志文件留在当前目录。

**3. 过滤时钟中断日志**：lab2 之后时钟中断会频繁输出。日志里 `async:1` 是中断、`async:0` 是异常——**异常才是定位重点**：

```bash
grep 'async:0' int.log | head
```

---

## 一、手段一：`-d int` —— 异常与中断日志分析

**用途：定位首个异常。** QEMU 会把每次异常 / 中断记一行。黑屏时打开日志读第一行，是本课程最有效的基础调试手段。

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -nographic -d int -D int.log &
QPID=$!; sleep 3; kill $QPID
grep 'async:0' int.log | head -3
```

### cause 速查表（建议贴在设计笔记上）

| cause | 含义 | 常见故障原因 |
| --- | --- | --- |
| `1` | 取指访问错（Instruction Access Fault） | **lab1 常见**：PMP 物理内存保护未正确配置；或跳到了未映射内存的地址 |
| `2` | 非法指令 | 跳进了数据段 / 没链接上的地址 |
| `5` / `7` | 读 / 写访问错 | 访问了未映射的 MMIO；设备基址错误 |
| `8` | U 态 ecall | **正常现象**（系统调用），不是 bug |
| `12` / `13` / `15` | 取指 / 读 / 写缺页 | lab3 后：页表映射错；lab5 后：先看是不是你的 COW 缺页分支 |
| `0x8000000000000005` | S 态时钟中断 | **正常现象**（lab2 起每 tick 一条） |

每一行还带 **`epc`**（出错指令地址）和 **`tval`**（出错访问的虚拟地址）——`tval` 比任何断点都直接。

---

## 二、手段二：`-d in_asm` —— 指令执行追踪

**用途：定位故障发生的指令与源码位置。**

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -nographic -d in_asm -D trace.log &
QPID=$!; sleep 3; kill $QPID
tail -40 trace.log
```

日志按"翻译执行的基本块"记录。**最后一块的最后一条指令，就是 CPU 停下前干的事。**

配合反汇编把地址翻译回源码行：

```bash
riscv64-unknown-elf-objdump -d kernel/kernel > kernel.asm
riscv64-unknown-elf-addr2line -e kernel/kernel 0x80000123   # epc/tval 填这里
```

`addr2line` 直接给出 `源文件:行号`（基线编译自带 `-ggdb`，符号全在）。**这两个命令是从"机器现场"回到"我的代码"的桥。**

---

## 三、手段三：QEMU Monitor —— 运行时状态与寄存器实时观测

### 交互式（内核还活着、只是行为不对）

在 QEMU 窗口按 `Ctrl-a c` 切到 QEMU 监视器：

```
(qemu) info registers      ← 全套 CSR：priv、pc、mstatus、scause、stvec...
(qemu) info cpus
(qemu) Ctrl-a c            ← 再切回串口控制台
```

**经典用法**：
- lab2 后怀疑"stvec 装错向量了" → 切 monitor 看 `stvec` 指到哪
- lab4 怀疑死锁 → 看各 hart 的 `pc` 停在谁身上

### 脚本式（需保存测试输出或自动化检测）

启动时加 `-monitor unix:/tmp/m.sock,server,nowait`，用课程平台提供的 `monpeek.py` 提取（它会解析并只打印 priv / pc / 关键 CSR）：

```bash
python3 monpeek.py /tmp/m.sock "info registers"
```

---

## 四、你自己的内核 = 你自己的调试器

上述三项手段聚焦**底层执行现场**；更常态的调试应结合内核自身构建的观测设施——**这些设施本来就是每轮实验的交付物**：

| 轮次 | 内部观测设施 | 调试用法 |
| --- | --- | --- |
| lab1 | 自己的 `printf` + banner | 裸机第一行输出就是断点："打印到哪一行之前停止"是黑屏定位第一手段 |
| lab2 | trap 处理里的 `printf("scause=%p sepc=%p", ...)` | 自己打印异常现场，直接对应你的数据结构 |
| lab3 | `dump_pagetable`（页表观测输出规范） | 页表对不对，dump 出来逐权限位核对 |
| lab4 | `schedstat` 观测调用 + `Ctrl-P` 进程快照 | 死锁 / 丢失唤醒：看每个进程卡在哪个状态、谁没被唤醒 |
| lab5 | `pmc(0)` / `pmc(1)` 计数 | COW 对不对，验证是否真正实现了只读共享 |
| lab6 | `crash_at(stage)` 受控断电 | 崩溃一致性验证的复现器，断点位置精确可重放 |

---

## 五、跨轮次的回归定位：`git bisect`

`-d int` 回答"**此刻**为什么崩"；`git bisect` 回答"从**哪个提交**开始崩"：

```bash
git bisect start lab6-submit lab2-submit   # 坏在 lab6，好的参考点是 lab2
git bisect bad                             # 当前(最新)是坏的
git bisect good                            # (bisect 给你切到的历史点)测一轮
# 每步：重新 make + 跑你的测试脚本，git bisect good/bad 标记
git bisect reset                           # 结束，回到最新
```

前提只有一个：**随做随 commit**（[学生须知 §四](05-学生须知.md#四git-版本管理全学期只用四条命令)）。提交历史越清晰，bisect 定位越精准。

---

## 六、速查：症状 → 第一动作

| 症状 | 第一动作 | 依据 |
| --- | --- | --- |
| lab1 黑屏且无输出 | `-d int` 读第一行 | `cause=1` → PMP；无日志 → mtvec / 入口没对 |
| 打印乱码 / 重复 | 查 LSR 轮询位与协议层 | 说明书附录 A |
| 系统调用陷入死循环无限重入 | 查 `sepc` 是否 `+4` | `-d int` 看 `epc` 是否未发生递增 |
| 键盘丢字 | 对照 `LAB2_BUF_SEMANTICS` / `BUF_SIZE` | `bufstorm` 测试 |
| 越界访问未被终止 / 异常终止 | `scause=15` + `tval` 地址 → `addr2line` → dump 页表 | `badaccess` 测试 |
| 调度卡死 / 运行不均衡 | `Ctrl-P` 快照 + `schedstat` 差分 | 官方调度测试（lab4 增量包 `support/tests/`） |
| 断电后文件系统损坏 | `crashmap` 表逐 stage 重放 | `crash_at` |
| 引入前期回归故障 | `git bisect` | 本文 §五 |
