# lab1 实测记录

## 构建与 ELF

测试环境为 WSL2 Ubuntu 22.04、`riscv64-unknown-elf-gcc` 和 `qemu-system-riscv64`。执行 `make clean && make` 无警告、无错误。

| 检查项 | 实测值 | 结果 |
| --- | --- | --- |
| ELF class / machine | `ELF64` / `RISC-V` | 通过 |
| 入口地址 | `_entry = 0x80000000` | 通过 |
| `machine_trap_vector` | `0x8000002c`，低两位为 0 | 通过 |
| 启动栈 | `boot_stack_top - boot_stack = 0x3000 = 12288` | 通过 |
| 链接警告 | 无 `_entry` 警告 | 通过 |
| 课程预置文件 | 与基线提交 `032f2ac` 比较无差异 | 通过 |

## 串口输出

QEMU 冷启动捕获结果：

```text
OSLAB1 sid=2024302141121 mod97=0x23
selftest zero=0 neg=-2147483648 max=2147483647 empty='' hex=0xffffffff long=01234567890123456789012345678901234567890123456789012345678901234567890123456789
[chk=12536]
```

校验和算法是校验和行之前所有逻辑字节的无符号 ASCII 字节和。独立脚本重新计算得到 `12536`，与内核输出一致。

## 自动检查结果

| 检查 | 结果 |
| --- | --- |
| QEMU 捕获与 `expect_banner.txt` 执行 `cmp` | 完全一致 |
| `python3 check_expect.py 2024302141121 expect_banner.txt` | `[ok]`，协议 2 形状通过 |
| 两次冷启动输出执行 `cmp` | 完全一致 |
| `grep -c 'async:0' int.log` | `0`，无同步异常 |
| Markdown 与 Git whitespace 检查 | 通过 |

## 现场问答定位

- 启动栈：`kernel/entry.S` 的 `boot_stack` 到 `boot_stack_top`，大小直接引用 `LAB1_STACK_KB`。
- PMP：`kernel/start.c` 的 `w_pmpaddr0` 与 `w_pmpcfg0`；删除后预计在第一条 S 态指令产生 `cause=1`。
- 协议 2：`kernel/console.c` 负责协议与校验和累计，`kernel/main.c` 在输出 footer 前暂停累计。
- 节流：`kernel/console.c` 的 `THROTTLE_PERIOD` 直接由 `COURSE_SID` 计算，物理输出每满 17 字节执行一次 `nop` 循环。
