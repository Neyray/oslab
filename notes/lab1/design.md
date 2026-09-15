# lab1 设计笔记

## 启动时序与不变式

```text
QEMU reset ROM @ 0x1000 (M)
  └─ 跳到 ELF 入口 _entry @ 0x80000000
       ├─ 清 mstatus.MIE 与 mie：任何初始化完成前都不接收中断
       ├─ mtvec ← machine_trap_vector：异常时进入可定位的停驻环
       ├─ mhartid != 0 → park：本轮仅允许 hart 0 继续
       ├─ sp ← boot_stack_top：12 KiB，向低地址生长
       └─ call start
            ├─ mstatus.MPP ← S，MIE 保持 0
            ├─ mepc ← main
            ├─ medeleg/mideleg ← 0xffff
            ├─ sie ← 0：lab1 没有 S 态中断处理器
            ├─ pmpaddr0/pmpcfg0：允许 S 态访问物理地址空间
            ├─ satp ← 0，sfence.vma：使用 Bare 模式
            ├─ tp ← mhartid
            └─ mret → main (S)
                 └─ console_init → banner/self-test → wfi 循环
```

进入 `main` 的核心不变式是：CPU 已在 S 态、全局中断关闭、地址转换关闭、PMP 已放行、`sp` 落在 12 KiB 启动栈内、只有 hart 0 会执行 C 代码。

## 为什么链接到 0x80000000

QEMU `virt` 的复位 PC 是 `0x1000`，这里是 QEMU 提供的少量复位 ROM。`-bios none` 表示没有 OpenSBI 等外部固件，但复位 ROM 仍负责把 `-kernel` 指定镜像的入口信息交给 CPU。RAM 从 `0x80000000` 开始，课程链接脚本也把 VMA 定在这里；若链接到 `0x0`，取指和数据访问会落入不存在的 RAM/MMIO 区间。

链接地址与加载地址必须一致：链接地址决定汇编和 C 代码中的符号地址，QEMU 则按照 ELF program header 把段装入相应物理地址。两者不一致时，即使第一条指令侥幸运行，后续 `la`、函数调用或数据引用也会跳到错误位置。

## entry.S 每一步的理由

1. 先清 `mstatus.MIE` 和 `mie`，关闭全局及各类 M 态中断源，避免栈、陷阱向量与特权级尚未准备好时进入不可恢复路径。
2. 在 `.balign 4` 的 `machine_trap_vector` 上设置 `mtvec`。低两位是模式字段，Direct 模式要求基址至少 4 字节对齐。
3. 读取 `mhartid`；非 0 核进入 `wfi` 停驻环。本轮的数据和 UART 路径没有并发保护，不能让多个核同时打印。
4. `sp` 设为 `boot_stack_top`。RISC-V 栈向低地址增长，顶部是第一个可用地址；空间大小直接引用 `LAB1_STACK_KB`，与个性化参数保持单一事实来源。
5. 调用 `start()`；若它意外返回，进入停驻环，避免执行到未定义区域。

## M 态到 S 态清单

- `mstatus.MPP = S`：决定 `mret` 的目标特权级。
- `mstatus.MIE = 0`：切换窗口保持不可中断。
- `mepc = main`：决定 `mret` 后的 PC。
- `medeleg = mideleg = 0xffff`：把可委派的异常和中断交给 S 态；lab1 同时保持 `sie=0`，所以不会实际接收设备中断。
- `pmpaddr0 = 0x3fffffffffffff`、`pmpcfg0 = 0xf`：NAPOT 覆盖物理地址空间并授予 R/W/X。缺失时，现代 QEMU 会在 `mret` 后第一条 S 态取指产生 instruction access fault。
- `satp = 0`：选择 Bare 模式。lab1 只需物理地址直接访问内核和 UART，不建立页表；恒等映射会引入本轮不需要的页表构造与错误面。
- `tp = mhartid`：保留当前 hart 编号，便于后续实验沿用。

## UART 与协议分层

`uartputc_sync()` 只负责硬件层：读取 `UART0 + 5` 的 LSR，等待 bit 5（THRE）为 1，再向 `UART0 + 0` 的 THR 写一个字节。启动早期没有时钟与中断驱动，忙等是可预测的选择；该函数没有超时，假设 QEMU 16550 可用。

`console_putc()` 负责课程协议层：协议 0/2 原样发射逻辑字节，协议 1 在每个逻辑字节后再发一个 `.`。物理发射函数记录实际字节数，每达到 `16 + COURSE_SID % 16 = 17` 字节执行固定长度 `nop` 循环并清零计数；周期表达式直接引用 `COURSE_SID`，不硬编码 17。

协议 2 的校验和选择“正文所有逻辑字节的无符号 ASCII 字节和”。正文包括 banner 与 printf 自检行，校验和行本身不参与计算。输出校验和前暂停累计，因此结果可稳定复现。

## printf 规范

- `%d`：有符号 `int` 十进制，包括 `INT_MIN`，用无符号减法取得绝对值，避免有符号溢出。
- `%u`：无符号十进制。
- `%x`：小写十六进制、无前导零。
- `%ld` / `%lu` / `%lx`：64 位 `long` 版本，用于打印学号。
- `%p`：`0x` 前缀加小写十六进制、无固定宽度前导零。
- `%s`：空字符串正常输出；空指针输出 `(null)`。
- `%c` 与 `%%`：单字符及百分号。
- 未知格式：原样打印 `%` 与格式字符，避免静默吞字节。

## 失败现象与定位

| 失误 | 预计现象 | 第一检查点 |
| --- | --- | --- |
| `_entry` 缺失 | 链接器警告并默认入口，QEMU 无稳定输出 | 链接日志、`readelf -h` |
| `sp` 错或栈容量不一致 | 刚调用 C 函数即异常或静默重启 | `objdump`、QEMU `-d int` |
| PMP 缺失 | S 态第一条指令取指异常，`cause=1` | `grep 'async:0' int.log` |
| `mtvec` 未对齐 | CSR 写入无效，异常后跳转失控 | `objdump` 查看向量地址低两位 |
| UART 基址/偏移错误 | 写访问异常或永远忙等 | `stval`、LSR/THR 地址 |
| `sepc` 或 MPP 错 | `mret` 后跳飞或仍停留 M 态 | QEMU Monitor 寄存器 |
| 协议包装层重复加点/漏点 | 输出可见但逐字节比对失败 | 比较 QEMU 捕获与 expect 文件 |
