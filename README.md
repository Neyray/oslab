# oslab · 操作系统实践

大三上《操作系统实践》课程工作区。**从零设计、累积构建**一个 RISC-V 内核：15 周，7 轮实验，同一棵代码树持续演进。

> ⚠️ 本仓库是**笔记与资料工作区**。内核代码树在第 1 周领取**个性化基线包**后单独建仓（见 [代码树](#代码树-labs)）。

---

## 目录

```
~/projects/oslab/
├── README.md            ← 你在这里
├── CLAUDE.md            AI 协作约定（课程 AI 政策 + 本仓库上下文）
├── docs/                课程文档（docx → md）
│   ├── 05-学生须知.md            规则、评分、工作流、AI 政策
│   ├── 06-环境安装指引.md        工具链安装 + preflight 自检
│   ├── 07-无gdb调试手册.md       ★ 全学期最常翻的一份
│   └── lab0-实验说明书.md        热身实验任务书
├── notes/
│   └── lab0/            三份交付图纸 + 思考题
├── materials/           原始 docx / pdf / preflight.py
└── reference/           MIT xv6-riscv 参考源码树（阅读用，不改）
```

---

## 课程规则速览

> 完整规则见 [`docs/05-学生须知.md`](docs/05-学生须知.md)。

| 项 | 内容 |
| --- | --- |
| **总评** | 现场验收 70%（lab1 10%，lab2–7 各 15%）+ 期末考查 30%（第 14 周设计考查 10 分 + 第 15 周综合报告 20 分） |
| **验收** | 每轮**只有一次**面对面验收：跑通功能 + 通过早期实验回归 + 三题问答 |
| **定档** | 功能测试定上限，**问答表现定档位** |
| **基线** | 单核 `-smp 1` 即可评 A 档（95）；lab4 起双核 `-smp 2` 通过 **额外 +3**，各轮独立、无连带 |
| **代码** | 第 1 周发个性化基线（含按学号派生的 15 项参数宏），之后每两周发统一增量包。**无标准答案** |
| **AI** | 允许并鼓励使用，无需提交记录；但**问答围绕你自己的参数、数据结构、回滚路径**，背答案无效 |
| **回归** | 每轮验收含一条早期实验回归测试，前期缺陷必须自行修复，教师不代改 |
| **补验** | 逾期或 E 档可在结束后一周内申请一次集中补验，最高按 60 分（D）记 |

### ⚠️ 关于 xv6 公开版

MIT xv6 是**合法且推荐**的原理阅读参考，但课程有公开版没有的专属规范：定制系统调用号、用户程序内嵌加载机制、诊断输出规范、按学号派生的参数宏。**直接复制公开版代码过不了测试，更过不了现场问答。**

---

## 环境状态

WSL2 (Ubuntu-22.04) + ext4，代码在 WSL 文件系统内（**不要放 `/mnt/c/`**，跨文件系统 I/O 慢 10 倍以上）。

| 工具 | 状态 |
| --- | --- |
| `riscv64-unknown-elf-gcc` / `-ld` / `-objdump` / `-addr2line` | ✅ `/usr/bin` |
| `qemu-system-riscv64` | ✅ `/usr/bin` |
| `python3` / `perl` / `git` / `make` | ✅ |
| `riscv64-unknown-elf-gdb` | ⬜ MISSING —— **正常**，本课程不用 gdb |

重新自检（教师第 1 次课查完整输出截图）：

```bash
python3 ~/projects/oslab/materials/preflight.py
```

安装 / 排障见 [`docs/06-环境安装指引.md`](docs/06-环境安装指引.md)。

---

## lab0（热身）· 第 1 周

追踪 Shell 执行 `echo hi` 的全生命周期。**不计分，但 lab1 验收时当场抽问这三份材料。**
任务书：[`docs/lab0-实验说明书.md`](docs/lab0-实验说明书.md)

| 材料 | 文件 | 状态 |
| --- | --- | --- |
| 一：全系统控制流图（模块级） | [notes/lab0/control-flow.md](notes/lab0/control-flow.md) | ⬜ 未开始 |
| 二：核心数据结构全景快照 | [notes/lab0/data-structures.md](notes/lab0/data-structures.md) | ⬜ 未开始 |
| 三：一次时钟中断的微观旅程 | [notes/lab0/timer-interrupt.md](notes/lab0/timer-interrupt.md) | ⬜ 未开始 |
| 思考题作答 | [notes/lab0/thinking-questions.md](notes/lab0/thinking-questions.md) | ⬜ 未开始 |

**预计 10–12 小时**（控制流图 6–8h，快照 4h）。超过 10 小时还没理清控制流图就带草稿去求助。

### 阅读路线（按序，勿按文件字母序孤立读）

- [ ] 1. `user/sh.c` 的 `main` 循环 —— 用户视角
- [ ] 2. `user/usys.pl` + `kernel/syscall.c` —— 系统调用如何跨特权级
- [ ] 3. `kernel/trampoline.S` + `kernel/trap.c` —— 特权边界（先读 C，汇编只看注释与跳转）
- [ ] 4. `kernel/exec.c` + `kernel/vm.c::uvmcopy` —— echo 进程从何而来
- [ ] 5. `kernel/file.c` + `kernel/console.c` —— `"hi"` 如何到外设
- [ ] 6. xv6 book《Operating system interfaces》《Traps and system calls》精读，与草图互校

每读一个核心函数问三件事：**谁调用它？会不会阻塞睡眠、谁唤醒？错误分支怎么回滚返回？**

### ⚠️ 命名差异

参考树是新版上游，与旧版 book / 网上旧资料撞名属正常，**一律以树内实际命名为准**：

| 旧资料常见 | 参考树实际 |
| --- | --- |
| `usertrapret` | `prepare_return` |
| `sleep`（单段） | 已拆两段式，含 `sleep_prepare` |

### 验收自查

- [ ] 控制流图覆盖 `read → fork → exec → write → exit`，逻辑自洽
- [ ] 核心跳转标注了**栈归属与特权级转换**
- [ ] 三张快照字段完整，引用链路清晰（`file → inode`、`proc → pagetable`）
- [ ] 页表快照标出 **trampoline / trapframe 的虚拟地址边界与权限位**
- [ ] 时钟中断流程体现**现场保护、调度器交接、`sret` 恢复**
- [ ] **至少 3 处自主思考批注**（用 `💭` 标记便于统计）

> 手绘图扫描件放各 lab 的 `assets/`，在 md 里 `![](assets/xxx.png)` 引用。md 承载结构化文字、字段表与批注，手绘图是主体，两者互为索引。

---

## 参考资料

### xv6 参考源码树 · `reference/`

lab0 的阅读对象。放置后**只读不改**：

```bash
git clone https://github.com/mit-pdos/xv6-riscv.git ~/projects/oslab/reference/xv6-riscv
```

> 若课程平台指定了自己的参考树链接，以课程平台为准——课程树是**新版上游**（见上方命名差异）。

### xv6 book

[`materials/xv6-book-riscv-rev5.pdf`](materials/xv6-book-riscv-rev5.pdf)。lab0 重点：**Ch.1 Operating system interfaces**、**Ch.4 Traps and system calls**。

### 我的 MIT 6.S081 实验记录

[github.com/Neyray/xv6-labs-2020](https://github.com/Neyray/xv6-labs-2020) —— 按 lab 分支：
`util` `syscall` `pgtbl` `traps` `lazy` `cow` `thread` `net` `lock` `fs` `mmap` `riscv`

**用途与边界**：MIT lab 是在**已有** xv6 上打补丁，本课程是**从零构建**；两者接口约定不同（本课程有定制系统调用号、内嵌加载机制、诊断输出规范、个性化参数宏）。当作**机理与调试经验**的参考，不是代码来源。

对应关系粗略如下：

| 本课程 | 我的 6.S081 分支 |
| --- | --- |
| lab2 陷入 / 系统调用 / 控制台 | `syscall` `traps` |
| lab3 SV39 页表与物理内存 | `pgtbl` `lazy` |
| lab4 进程状态机与调度器 | `thread` `lock` |
| lab5 写时复制 COW | `cow` |
| lab6 缓冲区缓存与日志文件系统 | `fs` |

---

## 调试速查

> 完整手册：[`docs/07-无gdb调试手册.md`](docs/07-无gdb调试手册.md)。**本课程不用 gdb。**

```bash
# 退出 QEMU：Ctrl-a 松开 x

# ① 异常/中断日志 —— 黑屏第一动作，读第一行
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d int -D int.log &
QPID=$!; sleep 3; kill $QPID
grep 'async:0' int.log | head -3      # async:0 是异常，async:1 是中断（时钟噪声）

# ② 指令追踪 —— 最后一个基本块的最后一条指令 = CPU 停下前干的事
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d in_asm -D trace.log &
QPID=$!; sleep 3; kill $QPID; tail -40 trace.log

# ③ 地址 → 源码行（epc / tval 填进去）
riscv64-unknown-elf-objdump -d kernel/kernel > kernel.asm
riscv64-unknown-elf-addr2line -e kernel/kernel 0x80000123

# ④ QEMU Monitor：Ctrl-a c 切入 → info registers / info cpus → Ctrl-a c 切回
```

**cause 速查**：`1` 取指访问错（lab1 多为 PMP 未配）｜`2` 非法指令｜`5`/`7` 读写访问错（MMIO 基址）｜`8` U 态 ecall（正常）｜`12`/`13`/`15` 缺页（lab3 页表 / lab5 COW 分支）｜`0x8000000000000005` S 态时钟中断（正常）

---

## 代码树 · `labs/`

第 1 周领到个性化基线包后：

```bash
mkdir -p ~/projects/oslab/labs && cd ~/projects/oslab/labs
# 解压基线包到此处，然后立刻：
git init && git add -A && git commit -m "baseline: 个性化初始基线包"
```

全学期只需四条 git 命令（[学生须知 §四](docs/05-学生须知.md#四git-版本管理全学期只用四条命令)）：

```bash
git init                                    # 领到基线立即建仓
git tag labN-submit                         # 每轮完成打标签
git archive -o labN-<学号>.zip labN-submit  # 导出归档，评测与备查证据
git push                                    # 推私有远程备份（强烈推荐）
```

出问题：`git checkout` / 标签回滚；跨实验隐蔽回归用 `git bisect`（[手册 §五](docs/07-无gdb调试手册.md#五跨轮次的回归定位git-bisect)）。

> Commit 历史不打分，但验收被问"这段代码是你写的吗"时，**清晰的提交记录是最有力的证明**。

---

## 每轮实验工作流

1. **先做设计笔记**——思考题不判分但验收直接抽选。动手前想透数据结构、不变式、回滚路径
2. **在自己的树上实现**——🚫 严禁修改包内系统预置文件（它们定义硬件与系统的接口边界）
3. **先于实现写两个自测**——覆盖边界条件与错误路径，以及只有你的参数才有的行为
4. **打标签归档**——`git tag labN-submit` + `git archive`
5. **按时验收**——功能 + 回归 + 三题问答

| | 官方测试 | 自写测试 |
| --- | --- | --- |
| 来源 | 随增量包发放 `support/tests/` | 你自己写，**先于实现** |
| 作用 | 全员一致的基础规范与正确性 | 逼你想清规格边界；覆盖个性化参数行为 |
| 批改 | 计分（验收现场跑） | 不批改，归档备查，**问答会抽问** |

---

## 学期路线

| 轮次 | 主题 | 观测设施（交付物，也是调试器） |
| --- | --- | --- |
| lab0 | 阅读与剖析（热身，通过制） | 三份图纸 |
| lab1 | 裸机启动与输出 | 自己的 `printf` + banner |
| lab2 | 陷入、系统调用与控制台驱动 | trap 里 `printf("scause=%p sepc=%p")` |
| lab3 | SV39 页表与物理内存管理 | `dump_pagetable` |
| lab4 | 进程状态机与调度器 | `schedstat` + `Ctrl-P` 进程快照 |
| lab5 | 写时复制 COW 与系统调用 | `pmc(0)` / `pmc(1)` 计数 |
| lab6 | 缓冲区缓存与日志文件系统 | `crash_at(stage)` 受控断电 |
| lab7 | 综合故障排查与现场系统设计 | 全部 |

> 后期实验频繁复用前期模块：lab5 的 COW 挂在 lab2 写的陷入分发上；lab6 的文件系统替换前期的用户程序加载方式。**前期的债后期一定会还。**
