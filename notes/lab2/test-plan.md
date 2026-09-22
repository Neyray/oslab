# lab2 指令验收清单

本文件按现场验收顺序组织：干净构建、Lab1 Banner 回归、Shell 启动、系统调用主链、非法调用、输入缓冲、忙循环中断活性、异常日志，最后核对提交与标签。每项同时列出执行指令、通过标准和当前实测结果。

## 0 验收前状态核对

~~~bash
cd ~/projects/oslab
git status --short --branch
git log -3 --oneline --decorate
cd labs/2024302141121-kernel
~~~

通过标准：工作树没有未提交改动；历史包含增量包提交和功能提交；`lab2-submit` 指向包含最新版代码与笔记的待验收版本。

当前验收结果：**PASS**。提交历史依次包含课程增量包、功能实现和验收笔记整理，`lab2-submit` 指向最后一笔文档提交：

~~~text
c27969a chore(lab2): merge course increment package
949fdd7 feat(lab2): implement traps syscalls and interrupt console
docs(lab2): standardize design and acceptance notes
~~~

## 1 干净构建

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean
make -j1
~~~

通过标准：两条命令退出状态均为 0；内核、`sh/hi/spin/badecall/bufstorm` 五个用户程序全部生成；编译和链接无 warning、无 error。

当前验收结果：**PASS**。从空构建产物开始完整编译成功。

## 2 Lab1 Banner 回归

先运行 Lab1 形状校验：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 check_expect.py 2024302141121
~~~

通过标准：输出包含：

~~~text
[ok] 形状校验通过(sid=2024302141121, 协议2)
~~~

再捕获 Lab2 内核启动输出的前三行，与 Lab1 期望文件比较：

~~~bash
timeout 3s qemu-system-riscv64 \
  -machine virt -bios none \
  -kernel kernel/kernel -nographic \
  > /tmp/lab2-boot.txt 2>/dev/null || true

head -n 3 /tmp/lab2-boot.txt > /tmp/lab2-banner.txt
cmp -s /tmp/lab2-banner.txt expect_banner.txt
echo $?
~~~

通过标准：`cmp` 输出 `0`，前三行逐字节为：

~~~text
OSLAB1 sid=2024302141121 mod97=0x23
selftest zero=0 neg=-2147483648 max=2147483647 empty='' hex=0xffffffff long=01234567890123456789012345678901234567890123456789012345678901234567890123456789
[chk=12536]
~~~

当前验收结果：**PASS**。形状校验通过，实际前三行与 `expect_banner.txt` 一致，checksum 仍为 `12536`。

## 3 Shell 启动与个人参数

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
timeout 5s make qemu
~~~

通过标准：Banner 后输出个人 Lab2 参数并出现 Shell 提示符：

~~~text
lab2 ready: tick=3 buffer=64 semantics=1
sh>
~~~

当前验收结果：**PASS**。首次进入 U 态后稳定出现 `sh>`，没有 `user trap:` 或 `kernel trap:`。

## 4 hi 系统调用主链

现场交互指令：

~~~text
sh> hi
~~~

预期输出模式：

~~~text
hi: user program running, pid=<正整数>
sh>
~~~

自动化指令：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 support/inject_uart.py \
  --tree . \
  --script support/tests/smoke.script \
  --log smoke-inject.log
~~~

该脚本先执行 `hi`，再执行下一节的 `badecall`。本节通过标准：显示 `boot ok`，`hi: user program running, pid=[0-9]+` 对应 EXPECT 为 PASS，并重新出现 `sh>`。

当前验收结果：**PASS**。实测首次子进程 PID 为 2，证明 `fork → exec → getpid/write → exit → wait` 完整闭合。

## 5 非法系统调用错误路径

现场交互指令：

~~~text
sh> badecall
~~~

预期输出：

~~~text
TEST-1 PASS: unknown syscalls all return -1
sh>
~~~

自动化仍使用：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 support/inject_uart.py \
  --tree . \
  --script support/tests/smoke.script \
  --log smoke-inject.log
~~~

通过标准：`TEST-1 PASS` 的 EXPECT 为 PASS，90–99 号调用全部返回 `-1`，内核和 Shell 均未退出。

当前验收结果：**PASS**。自动驱动最终显示 `EXPECT 失败 0 条`。

## 6 输入缓冲边界与 bufstorm

自动化指令：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 support/inject_uart.py \
  --tree . \
  --script support/tests/bufstorm.script \
  --log bufstorm-inject.log
~~~

脚本执行顺序：

1. 在 Shell 中执行 `bufstorm`。
2. 注入一行 100 个 `x`，超过个人缓冲容量 64。
3. 依次注入 `two`、`three`、`four`。
4. 等待 `BUFSTORM lines=4`。

通过标准：允许超出容量的普通字符被丢弃，但换行边界不能丢失；程序必须完成四次读取、输出 `BUFSTORM lines=4`、内核不 panic，并重新进入 `sh>`。

当前验收结果：**PASS**。驱动显示：

~~~text
boot ok
EXPECT 'BUFSTORM lines=4': PASS
EXPECT 失败 0 条
~~~

本次实测读取统计为 `BUFSTORM lines=4 bytes=116`。

## 7 spin 期间的 UART 与时钟中断

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
(sleep 1; printf 'spin\n'; \
 sleep 1; printf 'uart-ok\n'; \
 sleep 1) | timeout 4s make qemu
~~~

通过标准：持续出现递增的 `spin N`；`uart-ok` 能插入输出流并得到回显；没有 `kernel trap:`。Shell 此时等待不退出的 spin 属于本轮最小进程模型的预期行为。

当前验收结果：**PASS**。实测 `uart-ok` 出现在 `spin 149` 与 `spin 150` 之间，之后 spin 继续运行，证明用户忙循环期间 UART 外部中断仍可进入和返回。

## 8 -d int 异常日志

Lab2 的正常系统调用也是同步异常 cause 8，不能再沿用 Lab1 的“所有 async:0 都必须为 0”。应运行一组完整交互后排除 cause 8：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
rm -f /tmp/lab2-int.log

(sleep 1; printf 'hi\n'; \
 sleep 1; printf 'badecall\n'; \
 sleep 1; printf 'bufstorm\n'; \
 sleep 1; printf 'one\n'; \
 sleep 1; printf 'two\n'; \
 sleep 1; printf 'three\n'; \
 sleep 1; printf 'four\n'; \
 sleep 1) | timeout 9s qemu-system-riscv64 \
  -machine virt -bios none \
  -kernel kernel/kernel -nographic \
  -d int -D /tmp/lab2-int.log \
  > /tmp/lab2-int-output.txt 2>&1 || true

grep 'async:0' /tmp/lab2-int.log \
  | grep -v 'cause:0000000000000008' \
  | wc -l

grep 'async:0.*cause:0000000000000008' \
  /tmp/lab2-int.log | wc -l
~~~

通过标准：排除 cause 8 后同步异常数为 0；ecall 数大于 0；串口输出包含 `TEST-1 PASS` 和 `BUFSTORM lines=4`。

当前验收结果：**PASS**：

~~~text
非 ecall 同步异常 = 0
正常 U 态 ecall = 171
~~~

时钟中断可用下列命令抽查：

~~~bash
grep 'async:1.*cause:0000000000000001' \
  /tmp/lab2-int.log | head -n 3
~~~

预期能看到 S 态软件中断 cause 1，这是当前 QEMU 上由 M 态定时器桥接产生的正常 tick。

## 9 预置文件完整性

课程预置文件必须保持与增量包提交一致：

~~~bash
cd ~/projects/oslab
git diff --name-only c27969a -- \
  labs/2024302141121-kernel/kernel/trampoline.S \
  labs/2024302141121-kernel/kernel/fcntl.h \
  labs/2024302141121-kernel/kernel/param.h \
  labs/2024302141121-kernel/kernel/stat.h \
  labs/2024302141121-kernel/kernel/syscall.h \
  labs/2024302141121-kernel/kernel/vm.h \
  labs/2024302141121-kernel/user
~~~

通过标准：无输出。

当前验收结果：**PASS**。预置 trampoline、接口头文件和用户程序自增量包提交后均无内容改动；Makefile 仅在目标文件阶段把 `trampsec` 放入可加载 `.text.trampoline`。

## 10 提交 标签与归档

~~~bash
cd ~/projects/oslab
git log -2 --oneline --decorate
git rev-parse lab2-submit
git status --short --branch
unzip -t \
  /mnt/d/BaiduSyncdisk/大学学习文件/大三上/OS实践/lab2/提交-lab2-2024302141121.zip
~~~

通过标准：

- `c27969a` 是课程增量包导入提交。
- `949fdd7` 是功能完成提交。
- `lab2-submit` 指向包含最新版 `AGENTS.md`、代码和笔记的文档提交。
- 工作树干净。
- 归档 CRC 检查无错误。

当前验收结果：**PASS**。归档已按更新后的 `lab2-submit` 重新生成并通过 CRC 检查；尚未推送远端。
