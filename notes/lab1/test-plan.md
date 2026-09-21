# lab1 指令验收清单

本文件只保留实验说明书要求的可执行验收。除构建前置步骤外，顺序与现场 Autograder 一致：Banner 逐字节比对、<code>-d int</code> 零同步异常、节流参数源码核查；最后补做任务书要求的 printf 边界与冷启动幂等性。

## 0 构建前置

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean
make
~~~

预期正确结果：

- 两条命令退出状态均为 0。
- 编译和链接无 warning、无 error。
- 不再出现找不到 <code>_entry</code> 或使用默认入口的链接警告。

## 1 Banner 逐字节一致性

先执行任务书提供的形状校验器：

~~~bash
python3 check_expect.py 2024302141121
~~~

预期正确结果：输出包含
<code>[ok] 形状校验通过(sid=2024302141121, 协议2)</code>，且不出现
<code>[FAIL]</code>。这表示脚本内部三项均通过：包含学号十进制、包含
<code>0x23</code> 且格式正确、协议 2 的最末段包含 ASCII 数字校验和且其后无正文。

再捕获内核实际串口输出并比较：

~~~bash
timeout 3s qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -nographic \
  > /tmp/lab1-actual.txt 2>/dev/null || true

cat /tmp/lab1-actual.txt
cmp -s /tmp/lab1-actual.txt expect_banner.txt
echo $?
~~~

预期串口正文必须与 <code>expect_banner.txt</code> 逐字节一致：

~~~text
OSLAB1 sid=2024302141121 mod97=0x23
selftest zero=0 neg=-2147483648 max=2147483647 empty='' hex=0xffffffff long=01234567890123456789012345678901234567890123456789012345678901234567890123456789
[chk=12536]
~~~

<code>cmp</code> 的预期结果：

~~~text
0
~~~

QEMU 输出后停在 <code>wfi</code> 循环，由 <code>timeout</code> 结束属于预期行为；验收标准是捕获内容与期望文件完全一致。

## 2 -d int 零同步异常

~~~bash
timeout 3s qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -nographic \
  -d int \
  -D /tmp/lab1-int.log \
  > /dev/null 2>&1 || true

grep -c 'async:0' /tmp/lab1-int.log || true
~~~

预期正确结果：

~~~text
0
~~~

<code>async:0</code> 表示同步异常；结果为 0 才满足任务书中 <code>-d int</code> 零异常检查的通过条件。

## 3 节流参数源码核查

~~~bash
grep -n '#define THROTTLE_PERIOD' kernel/console.c
grep -n 'physical_bytes' kernel/console.c
grep -n 'asm volatile("nop")' kernel/console.c
~~~

预期正确结果必须同时证明：

~~~text
#define THROTTLE_PERIOD (16UL + (COURSE_SID % 16UL))
physical_bytes 每发射一个物理字节加一，达到 THROTTLE_PERIOD 后清零
asm volatile("nop")
~~~

源码必须直接引用 <code>COURSE_SID</code>，不能把本学号算出的 17 硬编码为节流周期，并且循环体中必须存在真实 <code>nop</code> 指令。

## 4 printf 边界

~~~bash
grep -F \
  "selftest zero=0 neg=-2147483648 max=2147483647 empty='' hex=0xffffffff long=01234567890123456789012345678901234567890123456789012345678901234567890123456789" \
  /tmp/lab1-actual.txt
~~~

预期正确结果是原样输出同一整行，覆盖任务书要求的：

~~~text
数字 0
负数 -2147483648
最大整数 2147483647
空字符串 ''
小写十六进制 0xffffffff
连续 80 个字符的长字符串
~~~

## 5 冷启动幂等性

~~~bash
timeout 3s qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -nographic \
  > /tmp/lab1-run1.txt 2>/dev/null || true

timeout 3s qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -nographic \
  > /tmp/lab1-run2.txt 2>/dev/null || true

cmp -s /tmp/lab1-run1.txt /tmp/lab1-run2.txt
echo $?
~~~

预期正确结果：

~~~text
0
~~~

两次 QEMU 复位冷启动的串口输出必须逐字节一致。
