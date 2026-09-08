# oslab · 操作系统实践

《操作系统实践》课程工作区。课程采用从零设计、累积构建的模式：15 周，7 轮实验，同一棵代码树持续演进，目标是一个运行在 QEMU `virt` 机器上的 RISC-V 内核。

本仓库为**笔记与资料工作区**。内核代码树在领取个性化基线包后置于 `labs/` 并单独建仓（见[代码树](#代码树-labs)）。

---

## 目录结构

```
~/projects/oslab/
├── README.md            本文件，仓库总览
├── CLAUDE.md            AI 辅助工具的工作约定
├── docs/                课程发放文档（docx 转 md，便于检索与交叉引用）
│   ├── 05-学生须知.md            规则、评分、工作流、AI 政策
│   ├── 06-环境安装指引.md        工具链安装与 preflight 自检
│   ├── 07-无gdb调试手册.md       三项核心调试手段
│   └── lab0-实验说明书.md        热身实验任务书
├── notes/               各轮实验的笔记与交付材料，每轮一个目录
│   └── lab0/            → 该目录的 README 是 lab0 的索引与进度
├── materials/           课程原始 docx / xv6 book / preflight.py
└── reference/           MIT xv6-riscv 参考源码树（阅读用，只读，不纳入版本管理）
```

---

## 课程规则速览

完整规则见 [`docs/05-学生须知.md`](docs/05-学生须知.md)。

| 项 | 内容 |
| --- | --- |
| 总评 | 现场验收 70%（lab1 10%，lab2–7 各 15%）+ 期末考查 30%（第 14 周设计考查 10 分 + 第 15 周综合报告 20 分） |
| 验收 | 每轮一次面对面验收：跑通功能 + 通过早期实验回归 + 三题问答 |
| 定档 | 功能测试决定成绩上限，问答表现决定最终档位 |
| 基线 | 单核 `-smp 1` 为统一基线，可评至 A 档（95）；lab4 起双核 `-smp 2` 通过额外 +3，各轮独立、无连带影响 |
| 代码 | 第 1 周发放个性化基线（含按学号派生的 15 项参数宏），之后每两周发放统一增量包，无标准答案 |
| AI 政策 | 允许并鼓励使用，无需提交对话记录；验收问答围绕本人的参数、数据结构与回滚路径 |
| 回归 | 每轮验收含一条早期实验回归测试，前期缺陷须自行定位修复 |
| 补验 | 逾期或 E 档可在实验结束后一周内申请一次集中补验，最高按 60 分（D 档）记 |

### 关于 MIT 公开版 xv6

MIT xv6 是课程认可的原理阅读参考。课程实验另有公开版不具备的专属规范：定制系统调用号分配、用户程序内嵌加载机制、诊断输出规范、按学号派生的 15 项参数宏。公开版代码无法通过课程测试与针对个性化代码的现场问答，仅作机理阅读之用。

---

## 环境状态

WSL2 (Ubuntu-22.04)，代码位于 WSL 的 ext4 文件系统内。课程指引要求避免置于 `/mnt/c/`，跨文件系统 I/O 存在约一个数量级的性能损失。

| 工具 | 状态 |
| --- | --- |
| `riscv64-unknown-elf-gcc` / `-ld` / `-objdump` / `-addr2line` | 已安装 `/usr/bin` |
| `qemu-system-riscv64` | 已安装 `/usr/bin` |
| `python3` / `perl` / `git` / `make` | 已安装 |
| `riscv64-unknown-elf-gdb` | 未安装 —— 符合课程预期，本课程不使用 gdb |

重新执行自检：

```bash
python3 ~/projects/oslab/materials/preflight.py
```

安装步骤与排障见 [`docs/06-环境安装指引.md`](docs/06-环境安装指引.md)。

---

## 实验进度

每轮实验的任务分解、笔记与交付材料在各自的 `notes/labN/` 目录，该目录的 README 为当轮索引。

| 轮次 | 主题 | 内部观测设施（交付物，同时是调试手段） | 笔记 | 状态 |
| --- | --- | --- | --- | --- |
| lab0 | 阅读与剖析（热身，通过制） | 三份图纸 | [notes/lab0/](notes/lab0/) | 文字稿完成，待手绘 |
| lab1 | 裸机启动与输出 | 自实现 `printf` + banner | — | 未开始 |
| lab2 | 陷入、系统调用与控制台驱动 | trap 处理中的 `printf("scause=%p sepc=%p")` | — | 未开始 |
| lab3 | SV39 页表与物理内存管理 | `dump_pagetable` | — | 未开始 |
| lab4 | 进程状态机与调度器 | `schedstat` + `Ctrl-P` 进程快照 | — | 未开始 |
| lab5 | 写时复制 COW 与系统调用 | `pmc(0)` / `pmc(1)` 计数 | — | 未开始 |
| lab6 | 缓冲区缓存与日志文件系统 | `crash_at(stage)` 受控断电 | — | 未开始 |
| lab7 | 综合故障排查与现场系统设计 | 上述全部 | — | 未开始 |

后期实验复用前期模块：lab5 的写时复制在 lab2 实现的陷入分发中挂载缺页分支，lab6 的文件系统替换前期的用户程序加载方式。前期遗留缺陷将在后续轮次暴露。

---

## 每轮实验工作流

1. 先做设计笔记——思考题不判分，但验收问答从中抽选；动手前明确数据结构、不变式与回滚路径
2. 在自有代码树上实现——不得修改包内系统预置文件，预置文件规定硬件与系统的接口边界
3. 先于实现编写两个自测用例——覆盖边界条件、错误路径，以及仅由个性化参数决定的行为
4. 打标签与归档——`git tag labN-submit` + `git archive`
5. 按时参加现场验收——功能 + 回归 + 三题问答

| | 官方测试 | 自写测试 |
| --- | --- | --- |
| 来源 | 随增量包发放 `support/tests/` | 本人编写，先于实现 |
| 作用 | 检验全员一致的基础规范与正确性 | 明确规格与边界；覆盖个性化参数行为 |
| 处理 | 计分，验收现场运行 | 不批改，归档备查，问答抽问 |

### 笔记目录约定

- 每轮实验一个 `notes/labN/` 目录，目录内 README 为当轮索引（任务分解、进度、验收自查）
- 手绘图纸的扫描件置于该轮的 `assets/`，在 md 中以 `![](assets/xxx.png)` 引用
- md 承载结构化文字、字段表与批注，手绘图为主体，两者互为索引
- 自主思考批注统一以 `💭` 标记，便于按验收要求统计

---

## 参考资料

### xv6 参考源码树 · `reference/xv6-riscv`

课程指定的原理阅读对象，只读不修改。已置入，HEAD `9e3161a`（2026-09-04）：

```bash
git clone https://github.com/mit-pdos/xv6-riscv.git ~/projects/oslab/reference/xv6-riscv
```

该版本即课程说明书所述的**新版上游**，与旧版 book 及网络资料存在多处命名与结构差异（`prepare_return`、两段式 `sleep`、Sstc 时钟等），阅读时一律以树内实际代码为准。完整差异对照见 [notes/lab0/control-flow.md](notes/lab0/control-flow.md#命名与结构差异相对旧版-xv6-与网络资料)。

`reference/` 由 [.gitignore](.gitignore) 排除，不纳入本仓库版本管理。

### xv6 book

[`materials/xv6-book-riscv-rev5.pdf`](materials/xv6-book-riscv-rev5.pdf)。注意书的修订版早于参考树，函数命名以树为准。

### 已有的 MIT 6.S081 实验记录

[github.com/Neyray/xv6-labs-2020](https://github.com/Neyray/xv6-labs-2020)，按 lab 分支组织：
`util` `syscall` `pgtbl` `traps` `lazy` `cow` `thread` `net` `lock` `fs` `mmap` `riscv`

**用途与边界**：该仓库是 2020 年版 xv6，与课程参考树（2026 上游）差异显著——它仍是 `usertrapret` / `initcode` / `timervec` / `uartstart`，没有 `prepare_return` / `sleep_prepare` / `stimecmp`。且 6.S081 的实验形式是在已有 xv6 上补全模块，本课程为从零构建，接口约定另有专属规范。**仅作机理理解与调试经验的参照，不作为代码来源。**

主题上的大致对应：

| 本课程 | 6.S081 分支 |
| --- | --- |
| lab2 陷入 / 系统调用 / 控制台驱动 | `syscall` `traps` |
| lab3 SV39 页表与物理内存管理 | `pgtbl` `lazy` |
| lab4 进程状态机与调度器 | `thread` `lock` |
| lab5 写时复制 COW | `cow` |
| lab6 缓冲区缓存与日志文件系统 | `fs` |

---

## 调试速查

完整手册见 [`docs/07-无gdb调试手册.md`](docs/07-无gdb调试手册.md)。本课程不使用 gdb，统一采用 QEMU 自带调试能力与内核自身的观测设施。

```bash
# 退出 QEMU：Ctrl-a 松开后按 x

# 手段一 异常/中断日志 —— 黑屏时的第一动作，读第一行
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d int -D int.log &
QPID=$!; sleep 3; kill $QPID
grep 'async:0' int.log | head -3      # async:0 为异常，async:1 为中断（时钟噪声）

# 手段二 指令执行追踪 —— 最后一个基本块的最后一条指令即 CPU 停止前的动作
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d in_asm -D trace.log &
QPID=$!; sleep 3; kill $QPID; tail -40 trace.log

# 地址回溯源码行（epc / tval 代入）
riscv64-unknown-elf-objdump -d kernel/kernel > kernel.asm
riscv64-unknown-elf-addr2line -e kernel/kernel 0x80000123

# 手段三 QEMU Monitor：Ctrl-a c 切入 → info registers / info cpus → Ctrl-a c 切回
```

cause 速查：`1` 取指访问错（lab1 多为 PMP 未配置）｜`2` 非法指令｜`5`/`7` 读写访问错（MMIO 基址错误）｜`8` U 态 ecall（正常）｜`12`/`13`/`15` 缺页（lab3 页表映射 / lab5 COW 分支）｜`0x8000000000000005` S 态时钟中断（正常）

跨轮次回归定位：`git bisect start labN-submit labM-submit`。

---

## 代码树 · `labs/`

领取个性化基线包后：

```bash
mkdir -p ~/projects/oslab/labs && cd ~/projects/oslab/labs
# 解压基线包至此，随后建仓并完成初始提交
git init && git add -A && git commit -m "baseline: 个性化初始基线包"
```

课程要求的四条 git 命令（[学生须知 §四](docs/05-学生须知.md#四git-版本管理全学期只用四条命令)）：

```bash
git init                                    # 领到基线后立即建仓
git tag labN-submit                         # 每轮完成打标签锁定版本
git archive -o labN-<学号>.zip labN-submit  # 导出归档，作为评测与备查证据
git push                                    # 推送至私有远程仓库备份
```

异常时以 `git checkout` 或标签回滚；跨实验的隐蔽回归以 `git bisect` 二分定位（[手册 §五](docs/07-无gdb调试手册.md#五跨轮次的回归定位git-bisect)）。
