# lab2 指令验收清单

对应任务书第 4 节“交付与验收”：

| 任务书要求 | 本文位置 |
| --- | --- |
| 现场跑通 Shell 与输入输出链路：`make qemu` 进 sh，`hi`、`spin` 正常 | 1.2 |
| 回归测试：`inject_uart.py` 全部通过 | 1.3 |
| lab1 的 Banner 回归测试一项 | 1.4 |
| 自测用例：动手前写好两个测试用例，归档备查 | 2 |
| 归档：`lab2-submit` 标签 + zip | 3 |

所有命令在 WSL 中执行。“实测”记录 2026-09-28 在当前提交上的真实运行结果。

## 1 现场验收

### 1.1 构建

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean && make
~~~

通过标准：退出码 0；内核和 8 个用户程序（sh hi spin badecall bufstorm readcap readempty syserr）全部生成；没有 warning。**实测：通过。**

### 1.2 Shell 与输入输出链路（交互演示）

~~~bash
make qemu
~~~

| 输入 | 期望输出 | 实测 |
| --- | --- | --- |
| （启动） | Banner 三行 → `lab2 ready: tick=3 buffer=64 semantics=1` → `sh>` | 通过 |
| `hi` | `hi: user program running, pid=2`，再出现 `sh>` | 通过 |
| `badecall` | `TEST-1 PASS: unknown syscalls all return -1` | 通过 |
| `spin`，然后乱敲键盘 | `spin N` 不断递增，敲的字符穿插回显，不死机 | 通过：`abc` 出现在 `spin 145` 与 `spin 146` 之间 |

spin 不会退出，按 `Ctrl-a` 松开再按 `x` 结束 QEMU。

### 1.3 官方自动化测试

~~~bash
python3 support/inject_uart.py --tree . --script support/tests/smoke.script    --log smoke.log
python3 support/inject_uart.py --tree . --script support/tests/bufstorm.script --log bufstorm.log
~~~

| 脚本 | 内容 | 通过标准 | 实测 |
| --- | --- | --- | --- |
| `smoke.script` | `hi` + `badecall`（调用号 90–99） | `EXPECT 失败 0 条` | 通过 |
| `bufstorm.script` | 1 行 100 字节超长行 + `two`/`three`/`four` 三行 | `BUFSTORM lines=4`，`EXPECT 失败 0 条` | 通过，`bytes=79` |

`bytes` 的值取决于时机，在 79 和 116 之间都算正常（原因见 design.md 第 7 节）。

### 1.4 lab1 Banner 回归

~~~bash
python3 check_expect.py 2024302141121
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic > /tmp/l2.txt 2>/dev/null
head -n 3 /tmp/l2.txt | cmp - expect_banner.txt && echo BANNER_OK
~~~

通过标准：输出 `[ok]` 和 `BANNER_OK`。**实测：通过**，校验和仍为 12536。

### 1.5 异常日志

lab2 里每次系统调用都是一次 `async:0 cause:8`，属于正常现象，所以要先排除 cause 8 再统计：

~~~bash
(sleep 1; printf 'hi\nbadecall\n'; sleep 2) | timeout 4s qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -nographic \
  -d int -D /tmp/l2-int.log > /dev/null 2>&1
grep 'async:0' /tmp/l2-int.log | grep -vc 'cause:0000000000000008'
~~~

通过标准：输出 `0`。**实测：0。**

## 2 自测用例

任务书原文：“动手前写好两个测试用例（例如：向 read 传入恰好等于缓冲区容量的长度；空缓冲区时 read 的行为；非法系统调用号的返回值）。归档备查。”

任务书举的三个例子各写成一个用户程序（T2-1、T2-2、T2-3）。它们和 sh 一样内嵌进内核，由 [`tests/lab2_selftest.py`](../../labs/2024302141121-kernel/tests/lab2_selftest.py) 启动 QEMU，像人一样通过串口输入，并判定结果：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab2_selftest.py
~~~

也可以在 `make qemu` 里手动输入程序名运行，下面每个用例都写了手动操作方法。

### 用例 T2-1：read 长度恰好等于缓冲区容量（`user/readcap.c`）

| 步骤 | 输入 | 断言（程序内部） | 为什么是边界 |
| --- | --- | --- | --- |
| 1 | 63 个 `a` + 回车，共 64 字节 | `read(0, buf, 64)` 返回 64，最后一个字节是 `\n` | 缓冲区恰好装满（`w - r == 64`），满判断不能差一 |
| 2 | 100 个 `b` + 回车 | 每次 read 返回 1–64 字节，最后一次以 `\n` 结尾，总数在 64–101 之间 | 超过容量：可以丢字符，但不能丢换行 |
| 3 | `ok` + 回车 | `read` 返回 3 字节，内容是 `ok\n` | 溢出后下标和 `lines` 计数仍然一致 |

通过标准：输出 `READCAP PASS`，之后重新出现 `sh>`。手动操作：在 sh 里输入 `readcap`，按提示依次粘贴三行。

**实测：PASS**（`overlong_total=64 overlong_calls=1`，也就是满缓冲时 63 个 `b` 加上被保留的换行；其他几次运行出现过 97、99，是因为读者在输入过程中取走了一部分）。

### 用例 T2-2：空缓冲区时 read 的行为（`user/readempty.c`）

程序先打印 `readempty: ready`，然后执行 `read(0, &c, 1)`。

| 步骤 | 输入 | 期望（本人参数：字符流 1） | 为什么 |
| --- | --- | --- | --- |
| a | 什么都不输入，等 1 秒 | 不出现 `READEMPTY`：read 阻塞，不返回 0，也不返回 −1 | 空缓冲区时读者必须等待，而不是立刻返回 |
| b | 只按 `q`，不按回车 | 立刻出现 `READEMPTY n=1 byte=113` | 字符流：`input_readable()` 只要求 `r != w` |
| — | 回车 | 程序读掉剩下的换行后退出，回到 `sh>` | 行尾不会残留给 sh |

**实测：a、b 都 PASS。** 手动操作：输入 `readempty`，停一下，再只按 `q`。

### 用例 T2-3：非法系统调用号的返回值（`user/syserr.c`）

| 类别 | 请求 | 期望返回 |
| --- | --- | --- |
| 非法调用号 | `ecall` 调用号 0（合法范围 1–22 的下边界外） | −1 |
| | 调用号 23（`SYS_sync + 1`，上边界外） | −1 |
| | 调用号 −1 | −1 |
| | `open`（编号 15，已定义但本轮未实现） | −1 |
| fd / 长度 | `write(0,…)`、`write(3,…)`、`read(1,…)` | −1 |
| | `write(1, &c, -1)` | −1 |
| | `read(0, &c, 0)` | 0（不阻塞） |
| 用户指针 | `write(1, 0x20000, 4)`：超出 64 KiB 用户空间 | −1 |
| | `write(1, 0x10000-2, 8)`：起点合法，但跨过空间末尾 | −1 |
| 进程 | `exec("nosuch")`、没有子进程时 `wait`、`pause(-1)` | −1 |
| 存活 | 之后 `getpid()` > 0；回到 sh 后 `hi` 正常 | — |

通过标准：输出 `SYSERR PASS failures=0`，之后 `hi` 能运行。官方 `badecall` 测的是 90–99，这里补测的是紧贴合法范围两端的 0 和 23，以及“已定义但未实现”的编号。

**实测：PASS（`failures=0`），之后 `hi` 正常。** 手动操作：输入 `syserr`。

### 附加检查（任务书未要求）

| 编号 | 内容 | 实测 |
| --- | --- | --- |
| A1 | 8 个内嵌程序的 `main` 都在地址 0。exec 从平铺二进制的地址 0 开始执行，编写 `syserr` 时真实遇到过 `main` 不在 0 的问题 | PASS |
| A2 | 复制一份代码树，把 `LAB2_BUF_SEMANTICS` 改成 0 后重新构建，重跑 T2-1 和 T2-2：只按 `q` 时 read 继续阻塞，按回车后才返回 | PASS（T2-1、T2-2a、T2-2b 都通过） |

A2 证明两种语义都是真实可用的分支，而且改参数只需要改 `course_sid.h` 这一处。副本位于 `/tmp`，预置文件没有被修改。

**汇总实测：`== 13 passed, 0 failed`。**

## 3 提交与归档

~~~bash
cd ~/projects/oslab
git status --short                          # 应为空
git log -1 --oneline lab2-submit            # 与 main 相同
git archive --format=zip -o 提交-lab2-2024302141121.zip lab2-submit
~~~

预置文件完整性（应无输出；增量包导入提交是 `c27969a`）：

~~~bash
git diff --stat c27969a -- \
  labs/2024302141121-kernel/kernel/trampoline.S labs/2024302141121-kernel/kernel/syscall.h \
  labs/2024302141121-kernel/kernel/param.h labs/2024302141121-kernel/kernel/vm.h \
  labs/2024302141121-kernel/user/sh.c labs/2024302141121-kernel/user/hi.c \
  labs/2024302141121-kernel/user/spin.c labs/2024302141121-kernel/user/user.ld \
  labs/2024302141121-kernel/user/usys.pl labs/2024302141121-kernel/user/ulib.c \
  labs/2024302141121-kernel/user/printf.c labs/2024302141121-kernel/user/user.h
~~~

归档包放在 Windows 侧 `OS实践/lab2/提交-lab2-2024302141121.zip`。
