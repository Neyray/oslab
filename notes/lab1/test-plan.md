# lab1 测试计划

测试顺序遵循“先静态形状、再构建、再运行、最后异常日志”的原则，避免把格式错误和启动错误混在一起。

## 1 静态检查

- 五个实现文件不再只有骨架注释，且预置文件保持与基线提交 `032f2ac` 一致。
- `entry.S` 中 `_entry` 与机器态陷阱向量存在，向量地址满足 4 字节对齐。
- 启动栈空间表达式引用 `LAB1_STACK_KB`。
- 节流周期表达式引用 `COURSE_SID`，并存在真实 `nop` 指令。
- `start.c` 包含 PMP 两项配置和 `satp=0`。

## 2 构建与 ELF 检查

```bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean && make
riscv64-unknown-elf-readelf -h kernel/kernel
riscv64-unknown-elf-nm -n kernel/kernel | grep -E '_entry|machine_trap_vector|boot_stack|main'
riscv64-unknown-elf-objdump -d kernel/kernel > /tmp/lab1-kernel.asm
```

通过条件：无编译警告；ELF 为 RISC-V 64 位；入口是 `_entry = 0x80000000`；陷阱向量地址低两位为 0；`boot_stack_top - boot_stack = 12288`。

## 3 printf 边界

内核启动时直接输出一行自检，覆盖：

- `0`
- `-2147483648`（`INT_MIN`）
- `2147483647`（`INT_MAX`）
- 空字符串
- `0xffffffff`
- 连续 80 个字符的长字符串

这些字节计入协议 2 校验和，校验和行本身不计入。

## 4 输出一致性

```bash
timeout 3s make qemu > actual_banner.txt 2>&1 || test $? -eq 124
cmp -s actual_banner.txt expect_banner.txt
python3 check_expect.py 2024302141121 expect_banner.txt
```

若 `make qemu` 把命令本身写入重定向结果，则直接启动 QEMU，只捕获串口标准输出：

```bash
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic > actual_banner.txt 2>/dev/null || test $? -eq 124
```

## 5 异常与重启幂等性

```bash
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic -d int -D int.log \
  > run1.txt 2>/dev/null || test $? -eq 124
grep 'async:0' int.log

timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic > run2.txt 2>/dev/null || test $? -eq 124
cmp -s run1.txt run2.txt
```

通过条件：`async:0` 过滤结果为空，且两次冷启动输出逐字节一致。
