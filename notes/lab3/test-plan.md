# lab3 指令验收清单

按实验说明书 V3.1 第 4 节“交付与验收”的五项要求排列。实测日期：2026-10-13，WSL 单核环境。

验收前构建并进入 Shell：

~~~bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
make clean && make
make qemu
~~~

通过标准：构建成功，打印 Banner、页表 dump 和 `sh>`。**实测：通过。** 下文交互命令均在 `sh>` 中输入；退出 QEMU 按 Ctrl-a，松开后按 x。

## 1 自测用例

任务书要求两组自测，源码随提交归档，具体如下。

**用例一：保护页异常安全终止进程**（[guardtest.c 第 7–20 行](../../labs/2024302141121-kernel/user/guardtest.c#L7)）

- 操作：从用户 sp 推导栈页底端，在子进程中分别读、写栈下方两页 guard，共 4 次非法访问。
- 判据：4 个子进程均以非零状态退出，父进程 wait 正常回收；输出 `GUARDTEST PASS pages=2 failures=0`，Shell 继续运行。
- 实测：PASS；读缺页 cause 13，写缺页 cause 15。

**用例二：跨越物理页边界的受控拷贝**（[vmtest.c 第 8–16 行](../../labs/2024302141121-kernel/user/vmtest.c#L8)）

- 操作：sbrk(8192) 申请两页，从第一页尾前 8 字节放入字符串并 write；再向同一跨页缓冲区 read。
- 判据：完整输出 `CROSS-PAGE-OK!`；输入 `cross-input` 加回车，read 返回 12 字节且内容一致；输出 `VMTEST PASS` 并回到 Shell。
- 实测：PASS；跨页 write/read 均正确。

手工演示：

~~~text
sh> guardtest
sh> vmtest
VMTEST input-ready
cross-input
~~~

失败表现：子进程非法访问后仍存活、父进程无法回收、内核停机，或跨页内容缺失/损坏，均不通过。

自动执行两组自测、官方测试和附加回滚测试：

~~~bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --all-orders
~~~

**实测：25 passed, 0 failed。** 附加覆盖物理页分配顺序、walk/mappages/sbrk 回滚、fork/exec 在 OOM 后恢复及 spin/sh 并发；用户测试前后空闲页保持 `32650 → 32650`。

## 2 回归测试 inject_uart.py 与 lab1 Banner

~~~bash
cd /home/jerico/projects/oslab/labs/2024302141121-kernel
python3 support/inject_uart.py --tree . --script support/tests/smoke.script --log /tmp/lab3-smoke.log
python3 support/inject_uart.py --tree . --script support/tests/bufstorm.script --log /tmp/lab3-bufstorm.log
python3 check_expect.py 2024302141121
timeout 3s qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic > /tmp/lab3-banner.txt 2>/dev/null
head -n 3 /tmp/lab3-banner.txt | cmp - expect_banner.txt && echo BANNER_OK
~~~

通过标准与实测：

- **smoke：** hi 正常运行，badecall 输出 `TEST-1 PASS`，`EXPECT 失败 0 条`。实测通过。
- **bufstorm：** `BUFSTORM lines=4`，`EXPECT 失败 0 条`。实测通过。
- **lab1 Banner：** 形状校验通过，前三行逐字节一致，输出 `BANNER_OK`。实测通过，205 字节，校验和 12536。

timeout 返回 124 是主动结束 QEMU，属于预期。这里比较前三行 Banner；后面的页表 dump 和 Shell 输出不参与 Banner 比较。

## 3 归档提交

当前版本已提交到本地，`main`、`lab3` 分支和 `lab3-submit` 标签指向同一最终提交；`lab3-start` 保留在增量包导入提交 `a2570d0`。本次文档提交、lab3 分支和标签尚未推送远端。

核对版本并生成提交包：

~~~bash
cd /home/jerico/projects/oslab
git status --short
git log -1 --oneline lab3-submit
test "$(git rev-parse main)" = "$(git rev-parse lab3)" && test "$(git rev-parse lab3)" = "$(git rev-parse lab3-submit)" && echo VERSION_OK
git archive --format=zip -o /mnt/d/BaiduSyncdisk/大学学习文件/大三上/OS实践/lab3/提交-lab3-2024302141121.zip lab3-submit
python3 -c 'import zipfile; p="/mnt/d/BaiduSyncdisk/大学学习文件/大三上/OS实践/lab3/提交-lab3-2024302141121.zip"; z=zipfile.ZipFile(p); assert z.testzip() is None; print("ZIP_CRC_OK")'
~~~

通过标准：工作区干净，输出 `VERSION_OK` 和 `ZIP_CRC_OK`，归档包含代码、测试、设计笔记与官方资料。**实测：通过。**

## 4 现场验收内容

**页表观测与权限解释：** 启动时自动打印 `dump_pagetable`；在 Shell 等待输入时按 Ctrl-P 重放。默认 sh 的映射如下。

- 代码 `0x0 r-xu`，数据 `0x1000 rw-u`，用户栈 `0x4000 rw-u`。
- 两页 guard（`0x2000、0x3000`）无映射；trapframe `0x3fffffe000 rw--`，trampoline `0x3ffffff000 r-x-`。
- 输出符合 `DUMP-PAGETABLE begin/end`、`L2/L1`、`LEAF` 的 ABI；perm 四位依次为 R/W/X/U，非叶为 `----`。

实测：格式、映射权限和 Ctrl-P 重放通过；现场需本人逐项解释权限设置。

**非法访问拦截与官方测试：** 在 Shell 输入：

~~~text
sh> badaccess
sh> memcap
~~~

通过标准：badaccess 的 TEST-3a/3b 均 PASS，非法访问子进程被终止；memcap 的 TEST-4 PASS，达到配额返回 -1，已分配数据可用，Shell 继续运行。**实测：全部通过；本版本 memcap 在 2036 个 64 KiB 块后返回 OOM。** 第 1 节 guardtest 同时演示用户栈越界拦截。

**三题问答：** 准备以下思考题，并能解释自己实现的映射回滚与物理页释放职责。详见 [思考题](design.md#3-思考题) 与 [映射回滚设计](design.md#4-设计决策与失败回滚)。

1. SV39 中 TLB 未命中需要几次物理内存访问，硬件如何降低开销？
2. 为何先使 PTE 无效再释放物理页，反过来会导致什么问题？
3. copyin 与 memcpy 有何区别，直接用用户指针会产生什么攻击？

问答通过标准：能结合本人的参数与源码讲清因果；**现场口述待本人完成。**

## 5 可选加分与销账

带上本人绘制的 lab0 地址空间快照图，用不同颜色标注当前两页无映射 guard、静态恒等映射内核栈和同址 trampoline，并与实际 dump 对照解释。

通过标准：图纸标注与当前实现一致，能说明修正理由。**状态：待本人复核并完成图纸修正。**
