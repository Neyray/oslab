# CLAUDE.md

《操作系统实践》课程工作区。项目总览见 [README.md](README.md)。

## 这是什么

从零构建一个 RISC-V 内核的课程。15 周 / 7 轮实验 / 同一棵代码树累积演进。
本仓库是**笔记与资料区**；内核代码树在 `labs/`（第 1 周领到个性化基线包后单独 `git init`）。

- 环境：WSL2 Ubuntu-22.04，ext4，路径 `~/projects/oslab`（**不要迁到 `/mnt/c/`**，跨文件系统 I/O 慢 10 倍以上）
- 目标机：`qemu-system-riscv64 -machine virt -bios none`
- 工具链：`riscv64-unknown-elf-*`（**无 gdb，这是正常状态，不要建议安装或使用 gdb**）

## 课程的 AI 政策 —— 直接约束你的行为

课程**允许并鼓励**使用 AI 辅助理解概念、编写代码、设计测试，无需提交对话记录。**但**：

> 验收问答围绕**学生本人**的个性化参数、**他自己**选择的数据结构、**他自己**写的失败回滚路径发问。
> 解释不清因果关系，即使功能全通也无法通过验收。

因此在本仓库里：

- **解释机制优先于给出代码**。被问"怎么写"时，先说清"为什么是这样、边界在哪、错了会怎样"，再给实现。
- **不要代写 lab0 的三份图纸**（[notes/lab0/](notes/lab0/)）。说明书明确要求本人理解并亲手绘制；图上的盲区会在他自己写的内核里暴露。可以提问、可以校验、可以指出遗漏项，不要填空。
- 涉及设计决策（数据结构选型、不变式、回滚路径）时**给选项和权衡**，让他选，不要替他定。
- 写完代码后主动问："这段被抽问时你会怎么解释？"

## 硬约束

- 🚫 **不得修改系统预置文件**（引导 / 链接脚本 / 增量包内标注为预置的文件）——它们定义硬件与系统的接口边界，改了直接失分。
- 🚫 **不得从 MIT 公开 xv6 复制代码**。公开版是合法的**原理阅读**参考，但课程有专属规范：定制系统调用号分配、用户程序内嵌加载机制、诊断输出规范、按学号派生的 15 项参数宏。复制过不了测试，更过不了问答。
- ⚠️ **参数宏按学号派生**（15 项）。看到 `LAB*_*` 形式的宏先去基线包里查实际定义，**不要假设数值**。
- ⚠️ 每轮验收含**早期实验的回归测试**。改动前期模块时主动提示回归风险。

## 调试：不用 gdb

三条路径，按顺序试（完整版 [docs/07-无gdb调试手册.md](docs/07-无gdb调试手册.md)）：

1. `-d int -D int.log` → `grep 'async:0' int.log | head` —— 读**第一个异常**。`async:0` 是异常，`async:1` 是中断（lab2 起时钟中断刷屏，必须过滤）
2. `-d in_asm -D trace.log` → `tail -40` —— 最后一个基本块的最后一条指令 = CPU 停下前干的事
3. `objdump -d` + `addr2line -e kernel/kernel <epc或tval>` —— 从机器现场回到源码行
4. QEMU Monitor：`Ctrl-a c` → `info registers` / `info cpus` → `Ctrl-a c` 切回

跑 QEMU 加调试参数时**别用 `make qemu`**，直接起：

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
    -d int -D int.log &
QPID=$!; sleep 5; kill $QPID
```

退出交互式 QEMU：`Ctrl-a` 松开再按 `x`。

**cause 速查**：`1` 取指访问错（lab1 多为 PMP 未配 / 跳到未映射地址）｜`2` 非法指令｜`5`/`7` 读写访问错（MMIO 基址错）｜`8` U 态 ecall（正常）｜`12`/`13`/`15` 缺页（lab3 页表映射 / lab5 先看 COW 分支）｜`0x8000000000000005` S 态时钟中断（正常）

跨轮次回归用 `git bisect start labN-submit labM-submit`。

## Git 约定

全学期只用四条：`git init` / `git tag labN-submit` / `git archive` / `git push`。

- 随做随 commit —— 这是 `git bisect` 能用的前提，也是"这代码是你写的吗"的证明
- 每轮完成打 `labN-submit` 标签锁定版本
- 归档：`git archive -o labN-<学号>.zip labN-submit`

## 文档约定

- 课程 docx 已转 md 放 [docs/](docs/)，原件留在 [materials/](materials/)。**改 md 不改原件**；若课程发新版 docx，重新转换而不是手改。
- 笔记里的自主思考批注统一用 `> 💭` 开头 —— 验收要求"至少 3 处自主思考痕迹"，这个标记便于统计。
- 手绘图扫描件放各 lab 目录的 `assets/`。
- 中文正文，代码 / 寄存器 / 函数名保持原文。

## 单核 / 双核

基线是单核 `-smp 1`，**单核就能评 A 档（95）**。lab4 起双核 `-smp 2` 通过额外 +3，各轮独立选择、无连带影响。默认按单核做，除非他明确说要挑战双核。
