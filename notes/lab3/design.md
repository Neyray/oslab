# lab3 设计与验收问答

依据 [lab3 实验说明书 V3.1](../../docs/lab3-实验说明书-V3.1.md) 与 [v2 增量包接口约定](../../materials/lab3-v2-20260929/能力目标与接口约定.md)。第 1–3 节讲清个人参数、系统主线与三道原题，第 4–7 节说明设计取舍、现场短答、追问与故障定位。正文按当前源码函数定位；`💭` 是基于真实测试或不变式的思考底稿，仍需本人复核、形成自己的口述解释与手绘图。

## 目录

1. [个人参数速查](#1-个人参数速查)
2. [系统主线与逐模块实现](#2-系统主线与逐模块实现)
3. [思考题](#3-思考题)
4. [设计决策与失败回滚](#4-设计决策与失败回滚)
5. [现场三题速答](#5-现场三题速答)
6. [高频追问](#6-高频追问)
7. [故障现象与日志定位](#7-故障现象与日志定位)

## 1 个人参数速查

参数唯一来源为 [`kernel/course_sid.h`](../../labs/2024302141121-kernel/kernel/course_sid.h)，没有修改原件。

| 参数 | 本人值 | 在实现中的意义 |
| --- | ---: | --- |
| `LAB3_ALLOC_ORDER` | 2 | 在当前全部空闲页按物理地址排序后，选下中位数 `(nfree−1)/2` |
| `LAB3_GUARD_PAGES` | 2 | 代码/数据后、用户栈前，留两个没有映射的虚拟页 |
| `LAB3_MEMCAP_DELTA` | 1 | 物理托管区上端保留一页，不能分配 |
| `PGSIZE` | 4096 | 分配、页表、映射的基本粒度 |
| `USERSTACK` | 1 | 每进程一页用户栈 |
| `PHYSTOP` | `0x88000000` | RAM 从 `0x80000000` 起，共 128 MiB |
| `MAXVA` | `0x4000000000` | 本内核只使用 SV39 低半区 |
| `TRAPFRAME` / `TRAMPOLINE` | `0x3fffffe000` / `0x3ffffff000` | 用户页表顶端两页，均没有 U 权限 |
| 进程槽 / 内核栈 | 8 / 每槽 8 KiB | 沿用过渡进程框架；内核栈静态分配并恒等映射 |

“中点”在增量包中只给了名称，没有规定偶数项取哪一侧。本实现明确采用**当前空闲集合的下中位数**，不是固定在物理区间中点轮流取页。另一个可选方案是从固定中点向两侧扩散；两者在释放后再分配时不同，现场应先说明自己采用的精确定义。内核自测独立维护地址排序数组，逐次核验所选地址。

托管区是 `[PGROUNDUP(kernel_end), PHYSTOP−DELTA×PGSIZE)`。`kernel_end` 由预置链接脚本导出，不能写成旧版 xv6 的 `end`。内核镜像、静态栈和管理元数据不属于空闲页；已托管区域中，页表页、trapframe 与用户数据页共用同一配额，没有另藏一份“不计账”的页。

## 2 系统主线与逐模块实现

### 2.1 从 lab2 到 lab3

| 模块 | lab2 的阶段性实现 | lab3 当前实现 | 源码位置 |
| --- | --- | --- | --- |
| 物理内存 | 静态用户数组，无通用页分配器 | 位图 + Fenwick 计数索引，按页分配与回收 | `kalloc.c:kinit/kalloc/kfree` |
| 页表 | 静态五张用户页表、内核 1 GiB 大页 | 动态三级页表，统一使用 4 KiB 叶子页 | `vm.c:walk_internal/map_pages` |
| 装载 | 整个 64 KiB 区域 RWX，入口假定为 0 | 构建期提取 ELF 入口与边界，区分 RX / RW | `tools/embed.py:elf_layout`、`proc.c:proc_exec` |
| 用户指针 | 用户数组基址 + 偏移 | 范围检查 + 逐页检查 + 经物理地址拷贝 | `proc.c:proc_copy*`、`vm.c:copy_user/copyinstr` |
| 堆 | 未实现 sbrk | EAGER 增长与收缩，失败整体回滚 | `proc.c:proc_sbrk`、`vm.c:uvm_grow` |
| 非法访问 | 打印后整个系统停住 | 终止当前用户进程，wait 返回非零状态 | `trap.c:usertrap`、`proc.c:proc_exit` |
| 执行机会 | 父进程 wait 时才切子进程 | 用户时钟边界轮转；输入/定时等待可让出机会 | `proc.c:select_next/proc_defer/proc_wake_*` |

实际 lab2 已为跳板和 VA 0 做了最小 SV39 映射，不能机械套用任务书“旧系统完全没开分页”的描述。这次的重点是把静态过渡页表换成动态、可回滚、可验证权限的通用机制。

### 2.2 启动链路

~~~text
_entry → start → mret → main
  → console_init → 原 Banner 三行（含 chk=12536）
  → vm_init
       kinit：托管 kernel_end 之后的物理页
       构建内核页表：RAM / UART / PLIC / CLINT / trampoline
       sfence.vma → satp=SV39|根页号 → sfence.vma
  → trap_init：PLIC / UART / SSIP
  → proc_init
       分配 trapframe 与根页表 → proc_exec("sh")
       dump_pagetable → lab3 ready
  → proc_enter_user → usertrapret → 高地址 userret → sret
~~~

本机时钟仍采用 `start.c` / `timervec.S` 的 **M 态 CLINT → S 态软件中断**桥接，S 态 cause 是中断 1。没有切换到任务书示例中的 Sstc CSR，避免访问当前 QEMU 不支持的 `stimecmp`。原引导、时钟汇编和链接脚本没有修改。

### 2.3 用户空间快照（默认 sh 的实际映射）

~~~text
0x0000 ─ 0x1000    代码/只读内容     r-xu
0x1000 ─ 0x2000    数据/bss           rw-u
0x2000 ─ 0x3000    guard 1            无映射
0x3000 ─ 0x4000    guard 2            无映射
0x4000 ─ 0x5000    用户栈             rw-u，向低地址增长
0x5000             初始 heap_base=sz，堆向高地址增长
...                未映射
0x3fffffe000       trapframe          rw--
0x3ffffff000       trampoline         r-x-
~~~

不同程序的镜像大小可能不同。实际公式是 `image_end=PGROUNDUP(memory_end)`，`stack_base=image_end+2×PGSIZE`，`heap_base=stack_base+USERSTACK×PGSIZE`，不能把 sh 的 0x5000 硬编码进装载器。地址空间遍历与 fork 跳过 guard 空洞。

lab0 参考树快照中的 guard 曾是“分配物理页、清 U 位”。当前选择“完全不映射”，两者都能阻止 U 态访问；当前方案不消耗 guard 的物理页。lab0 中高地址 `KSTACK` 映射本轮未采用：静态内核栈位于 RAM 恒等映射内，所以应在本人图纸上注明这项差异，不能把高地址内核栈画成已经实现。

### 2.4 用户段权限如何传到内核

~~~text
原 user/user.ld（只读）
 → tools/embed.py linker 生成 user-flat/user.ld
     在数据段前加 4KiB 对齐，收集 .sdata/.sbss
 → 编译 ELF + objcopy 平铺镜像
 → embed.py table 读取 ELF 节表与入口
 → 每项 {start,end,entry,readonly_end,memory_end,name[32]}
 → proc_exec 按页选择 RX 或 RW，清零 bss，epc=entry
~~~

内嵌表先完整连续生成，再放各程序镜像；终止项为 72 字节零记录。C 侧用编译期断言核对表项大小。程序的链接基址仍为 0，但入口不再被假定为 0：辅助函数可能排在 main 前，ELF 的入口值才是事实。lab2 自测的 A1 相应改为检查“ELF entry 等于 main”，其余输入输出与错误路径断言保留。

> 💭 若数据与代码落在同一页，“给代码去掉 W”会同时让合法的数据写入失败，而“给数据加 W”又会让写代码的测试错误存活。生成派生链接脚本，把数据起点对齐到新页，才能从根本上解决页粒度权限冲突；原预置脚本仍保持逐字节不变。

### 2.5 一次跨页 write

1. 用户把 fd、VA、长度放进 a0/a1/a2，a7=16，执行 ecall。
2. `uservec` 保存用户现场，切内核页表，调用 `usertrap`；C 入口先把 stvec 改为 kernelvec。
3. `sys_write` 先检查整段范围与 R/U 权限，再用 128 字节内核缓冲分块。
4. `copyin` 每页取得 PTE 的物理页地址，以 `min(剩余长度, 本页剩余字节)` 为长度拷贝；下一页重新查表，不假定物理连续。
5. 内核输出缓冲内容，返回实际长度；经同址 trampoline 切回用户页表并 sret。

`read` 检查的是 W/U，代码页即使可读也不能成为写回目标。`copyinstr` 每页检查 R/U，按 NUL 结束；字符串跨页时重新查表。整个拷贝范围会先预检，所以 copyout 在后页无权限时不会先修改前页。

## 3 思考题

### 3.1 在 SV39 分页模式下，发生一次 TLB 未命中的地址翻译需要访问几次物理内存？现代体系结构依靠何种硬件机制降低该开销？

**结论：** 本实现使用三级 4 KiB 页，最坏需要读取 3 次 PTE；若连最终指令或数据访问一起算，共 4 次物理内存访问。这是假定无页表遍历缓存命中、无异常的计数。

- satp 给根页表物理页号，VPN[2]/VPN[1]/VPN[0] 每级各 9 位，每次选一个 8 字节 PTE；最后拼接 12 位页内偏移。
- TLB 缓存 VA→PA 与权限，命中时免去三层遍历；页表遍历缓存、普通缓存、硬件并行与大页也可降低开销。本实现没有建立大页，不能用 lab2 的 1 GiB 叶子来解释当前遍历。
- TLB 缓存也带来失效责任。写 satp 或修改映射后，必须按当前单核设计执行 sfence.vma；否则新页表已变，CPU 还可能使用旧 PA 或旧权限。
- 源码对应 `vm.c:walk_internal`、`riscv.h:PX/PTE2PA/MAKE_SATP`；上述访问次数为架构推导，不是本轮性能实测。

### 3.2 拆除虚拟地址映射时，为何必须先将对应 PTE 置为无效、随后再释放底层物理页？若颠倒操作顺序会导致何种安全漏洞或系统故障？请列举具体的应用场景。

**结论：** 先撤销访问能力，再把物理页交给分配器；本实现的顺序是清 PTE → sfence.vma → kfree。

- 释放后该页可能马上交给另一个进程，甚至作为页表页。若旧 PTE 或旧 TLB 项仍有效，原进程仍可读写它。
- 具体例子：A 收缩堆，页被分给 B 保存私有数据；A 通过残留映射读取 B 的数据，或写坏 B 的页表，造成越权与崩溃。
- 只清 PTE 还不够，TLB 可能继续命中旧项。单核刷新后才回收；多核还需让其他持有该映射的 hart 刷新并确认完成，当前只支持单核。
- 源码对应 `vm.c:uvmunmap/free_level`，新中间表回滚也遵循先清父 PTE、刷新，再释放子表。

### 3.3 copyin 与标准库 memcpy 的本质区别是什么？若内核直接使用 memcpy 处理用户传入的指针，恶意程序可利用该缺陷实施何种攻击？

**结论：** copyin 把不可信的用户 VA 当作需要验证、翻译的输入；memcpy 只按当前地址空间直接访问两个指针，不理解用户页表或权限。

- 用户 VA 在内核页表中未必有相同含义，不能用它直接访存；即使某地址恰好落在内核恒等映射中，也不能把它视作合法用户缓冲区。
- 本实现先检查 `va+n` 不溢出、不超出进程 sz，再逐页检查有效位、U 位和 R/W，最后经内核恒等映射访问 PA。copyin 要 R，copyout 要 W，trapframe/trampoline 没有 U，因此均被拒绝。
- 恶意指针可导致内核读取敏感内存并通过 write 泄露，或让 read 写坏内核内存；无效地址还可直接触发内核异常造成拒绝服务。
- 源码对应 `proc.c:within_user/proc_validate/proc_copy*`、`vm.c:vm_check/copy_user`。`vmtest` 实测拒绝高地址、越界与溢出请求，内核仍能继续运行。

## 4 设计决策与失败回滚

### 4.1 空闲管理结构与配额

| 方案 | 分配 | 回收 | 元数据 / 取舍 |
| --- | --- | --- | --- |
| 有序单链表 | 升序取头 O(1)，中点需遍历 | 有序插入 O(n) | 节点可放空闲页；批量回收可能 O(n²) |
| 位图 | 搜索空闲位或第 k 位 O(n) | 标记 O(1) | 128 MiB 约 4 KiB 位图 |
| 当前：位图 + 分组 Fenwick 计数 | 按秩选组 O(log 512)，组内最多扫描 64 位 | O(log 512) | 4096 字节位图 + 513×4 字节计数，约 6 KiB |

位图 1 表示空闲；Fenwick 树记录每组空闲数量，`nfree` 是总数。分配时移除位，计数减一，然后清零整页；回收先验证范围、对齐与未重复释放，填充字节 1，再放回空闲集合。重复释放属于内核程序错误，会打印诊断停住；正常耗尽则返回 NULL，绝不因 OOM 停机。

分配操作在单核关中断的临界区完成，并恢复进入时的中断状态。没有声称支持双核并发；lab4 若扩展双核，需要真正的自旋锁和相应 TLB 协议。

> 💭 中点分配使相邻虚拟页更容易落在不连续的物理页。跨页测试因此能实际检出“物理地址加长度就直接拷贝”的错误，而不是只在连续分配时碰巧通过。

### 4.2 mappages 的资源所有权与回滚不变式

| 资源 | 谁创建 | 成功后归谁 | 失败时谁回收 |
| --- | --- | --- | --- |
| 待映射的数据物理页 | 调用方 kalloc | 地址空间 | 调用方；mappages 不释放它 |
| 本次新建中间页表 | 映射函数 | 页表树 | 映射函数 |
| 既有页表与叶映射 | 以前的操作 | 原地址空间 | 本次失败不得删除 |
| trapframe | 进程槽分配器 | 进程 | `release_slot`；vm_free 只去除其映射 |
| trampoline 物理页 | 内核镜像 | 全内核共享 | 不交给 kfree |

1. 先预检范围、对齐、权限合法性和重复映射。重复映射返回 -1，不覆盖原 PTE。
2. `walk_internal` 每次最多新建两层表；第二层分配失败时，逆序撤回本次 walk 已创建的层。
3. 映射事务用 RSW bit 8 临时标记新中间节点 `VM_NEW`，原节点不打标记。
4. 第 N 页失败时，清除前 N−1 页的本次叶映射并刷新 TLB；递归只回收带标记且已空的新表，已有空表也会保留。
5. 成功后清除临时标记。返回调用方时，不留 VM_NEW；它不是 COW 标志。

`uvm_grow` 把整个增长纳入同一个事务，并负责回收自己申请的数据页。旧 sz 只在全成功后更新。失败后应满足：旧叶映射不变、旧数据不变、空闲页数恢复、调用方拿到 -1。

> 💭 内核测试确实发现过：增长失败后用“全树清理空页表”做回滚，会把之前用 walk 创建的空表也释放。虽然旧数据映射未坏，空闲页数却不再恢复到调用前。现在整次增长共用事务标记，仅回收本次创建的节点；测试专门保留一张旧空表，验证其 PTE 地址与计数均不变。

### 4.3 guard 与用户异常

guard 不分配物理页，PTE 无效。读 guard 为 cause 13，写 guard 为 cause 15，跳入不可执行/未映射区为 cause 12。写 VA 0 的代码页也是 cause 15，但原因是缺 W，而不是地址 0 没有映射。

`usertrap` 首先切换 stvec，保存 epc；ecall 才加 4。未识别的用户异常打印 scause/sepc/stval 后调用 `proc_exit(-1)`，父进程 wait 得到非零状态，Shell 可继续运行。内核异常仍按内核 bug 诊断，不能把内核访问错误也伪装成普通用户退出。

### 4.4 copyin/copyout 策略与用户指针审查

逐字节查表最直观，但每字节重复 walk；纯按字拷贝容易跨页或未对齐；当前选择“预检全部页 + 按页分块拷贝”，每页查表次数有限，跨页边界明确。内核拷贝函数使用字节循环以避免对齐假设；性能优化可在权限检查后的页内做，不改变安全边界。

| 调用点 | 不可信输入 | 当前路径 |
| --- | --- | --- |
| `sys_write` | 用户源缓冲区 | `proc_validate(R)` + `proc_copyin` |
| `sys_read` | 用户目标缓冲区 | 读取前 `proc_validate(W)`，之后 `proc_copyout` |
| `sys_exec` | 用户名字字符串 | `proc_copyinstr` 到内核本地数组 |
| `proc_wait/reap` | 用户退出状态指针 | 等待前检查 W/U，回收时 copyout 到父页表 |
| fork / exec 的内部复制 | 已经由内核查表得到的 PA | `memory_copy`，不直接解引用用户 VA |

指针与长度检查采用减法边界式，避免 `address+length` 溢出。read 的目标非法时先返回 -1，不先消耗输入或等待输入。`wait(NULL)` 是显式允许的“不要状态”；非空只读指针返回 -1，而且子进程仍可由下一次合法 wait 回收。

### 4.5 sbrk 的返回值与边界

- a0 是带符号 int 增减量，a1 为 EAGER=1 / LAZY=2。当前两种均即时分配，其他策略返回 -1。
- 返回**旧边界**，即新分配区间起始地址；`sbrk(0)` 返回当前边界。任务书有“返回新边界”的措辞，但官方 memcap 随即从返回指针写入新申请的 CHUNK，必须返回旧边界才能符合其可执行 ABI。本实现以用户库和官方测试的实际调用方式为准，并在这里显式记录差异。
- n>0 按页补齐，允许字节级 sz；n<0 只回收不再被边界覆盖的完整页；禁止收缩到 heap_base 以下。
- LAZY 的缺页按需分配留给 lab5；本轮缺页属于非法访问，不会偷偷补页。

### 4.6 fork、exec 与 struct proc 生命周期

| 字段/对象 | 创建 | 切换或使用 | 销毁 |
| --- | --- | --- | --- |
| `pagetable/user_satp` | allocate_slot / new_space | usertrapret 传给 userret | exec 成功替换、或 release_slot |
| `trapframe` | 每个槽 kalloc 一页 | uservec 保存、userret 恢复；exec 复用同一物理页 | release_slot |
| `sz/heap_base` | exec 设置 | sbrk 提交后更新；fork 继承 | 释放槽清零 |
| `guard_start/stack_base` | exec 按镜像计算 | 观测、解释布局；fork 继承 | 释放槽清零 |
| `wait_status_address` | wait 设置 | 子进程退出后写回父空间 | 完成等待 / 清槽 |
| `sleep_until` | pause 首次调用设置 | 时钟唤醒后重试 ecall | 完成 pause 后清零 |
| 内核栈 | 静态每槽 8 KiB | trapframe.kernel_sp 指向栈顶 | 本轮保留，不动态回收 |

fork 只遍历 `[0,sz)` 的有效叶子，逐页分配、复制数据、保留权限；guard 空洞跳过。子 trapframe 是新分配的独立页，复制的是其中的寄存器内容；trampoline 的物理页共享。任何失败都释放尚未发布为 RUNNABLE 的子槽。

exec 的提交点在新空间完整建立后。此前只操作临时页表；失败释放新空间，原页表、sz、现场及数据均保留。成功才替换页表、设置 entry/sp 并释放旧空间。`oomtest` 在完全耗尽和只释放少量页的两种情况下验证 fork/exec 失败，再证明旧数据和后续分配可用。

### 4.7 内核映射、TLB 与运行边界

RAM 从 KERNBASE 到 PHYSTOP 全部恒等映射，包含分配器可能交出的页表页与数据页。预置 kernel.ld 没有导出 etext，当前 RAM 统一 RWX 是任务书允许的阶段性约束；用户代码仍严格不含 W。

UART 一页、PLIC 4 MiB 使用 RW；CLINT 64 KiB 也映射为 RW，便于明确完整外设清单，但当前实际定时访问在 M 态，M 态不经 S 态 satp 翻译。用户与内核都把同一个 trampoline 物理页映射到相同高 VA。返回用户不能在普通 C 指令中直接切 satp 后继续执行，必须先跳到高地址 userret。

mappages 提交/撤销、uvmunmap、释放页表与 satp 切换均执行本 hart 的 sfence.vma；跳板的两次切换也有预置刷新。叶 PTE 由软件预置 A，具备 W 时预置 D，避免依赖硬件自动置位；dump 的四位 perm 不输出 A/D。

用户态时钟中断边界可在 RUNNABLE 槽间轮转。read 无输入、pause 未到期时，`proc_defer` 把保存 epc 减回该 ecall，标记 INPUT/SLEEP，再选别的进程；UART/时钟只唤醒状态。系统调用分发器检查 epc 是否变化，防止把返回值覆盖到待重试的原始 a0 参数。wait 则由退出路径写入最终 PID，不重试。

**已经实现**：单核用户边界轮转、输入/定时等待、父子回收、预置 spin 与 sh 并发、动态虚拟内存。**没有实现**：任意内核 C 续体的 swtch、通用 sleep/wakeup 队列、个性化 lab4 调度策略、双核锁、COW、LAZY 缺页分配、文件系统、exec argv。这些是后续轮次的重构边界，不能把本轮过渡机制描述成完整调度器。

## 5 现场三题速答

| 问题 | 可以先口述的一句话 | 继续指出的代码 |
| --- | --- | --- |
| 第 N 页映射失败怎么办？ | 撤回本次叶映射，只释放本次新表；数据页由申请它的调用方回收，旧空间不变。 | `vm.c:map_pages/finish_mapping/uvm_grow` |
| 为什么 guard 触发异常但系统不死？ | guard 没映射；异常来自 U 态，就终止当前进程并给父进程非零状态，随后运行其他进程。 | `trap.c:usertrap`、`proc.c:proc_exit/reap` |
| 为什么 copyin 不能换 memcpy？ | 用户 VA 不等于内核地址；必须查用户表、验证 U 和读写权限，再经 PA 逐页复制。 | `proc.c:proc_validate`、`vm.c:copy_user` |

回答时先说结论，再展开硬件事实 → 软件动作 → 错误时的现象。不要只背“有页表就安全”，应能指出由哪一位、哪一层检查、哪条失败路径保证安全。

## 6 高频追问

**问：释放为什么填 1、分配为什么清 0？**
填垃圾值使悬挂指针的错误更易观察，且不会把链表/旧数据当作新对象；清零保证跨进程数据隔离，也让新页表初始全无效。填 1 不是权限保护，真正的保护来自 PTE 和失效顺序。

**问：walk 返回 PA 还是 PTE 指针？**
返回叶 PTE 的地址。调用方需要检查、设置、撤销权限和映射，只有 PA 无法修改页表。alloc=0 时缺失返回 NULL；alloc=1 可以创建中间表，但分配失败要撤回本次创建的层。

**问：为什么不是所有有效 PTE 都是叶子？**
中间表有效但 R/W/X 全为 0，PPN 指向下一层表。当前只在 L0 建叶子，叶子至少具有 R/W/X 之一；W 而没有 R 的权限组合会被拒绝。

**问：你用位图，是不是回收 O(1)？**
单独位图置位 O(1)，当前还维护 Fenwick 计数，所以整体 O(log 512)。用额外约 2 KiB 计数换来中点选择也只需对数查找，避免每次扫描全部页。

**问：怎样证明 OOM 没泄漏？**
内核测试把空闲页消耗到个位数，跨 1 GiB 分支强制映射中途失败，验证原空表、旧叶映射与计数恢复；用户测试在 OOM 后回收、再分配并验证数据；全部用户程序退出后，Ctrl-P 统计保持 32650→32650。具体数值随镜像变化，守恒关系才是不变式。

**问：Ctrl-P 为什么能打印，不需要新系统调用？**
UART 输入中断识别控制字符 0x10，调用 `proc_dump_current`，按当前进程页表打印。首个 sh 装配完成也自动打印一次；传入具体页表指针，未硬编码全局唯一页表。

**问：trampoline 预置修复合入后，为什么删旧 objcopy 重命名？**
v2 已给 trampsec 加 ax 和页对齐，链接器可以放置这个可加载孤儿段。首轮实测中继续重命名破坏了对齐重定位处理，trampoline 符号落在页尾，userret 越过映射边界，出现 epc=0x40000000a8 的取指缺页风暴。保持原段与重定位后，uservec/userret 落在同一页；原预置源码没有再改。

**问：fork 后为什么子进程返回 0？**
复制 trapframe 后，把子 a0 设为 0；父进程系统调用返回子 PID。两边从各自保存的 ecall 下一条指令继续，物理数据页和 trapframe 不共享。

**问：spin 永不退出，普通 sh 执行它会立刻返回提示符吗？**
不会，预置 sh 是前台 fork/exec/wait。并发演示程序 concurrent 另建 spin，然后把自身 exec 成预置 sh；此时 sh 等待输入会让出机会，spin 持续输出，sh 仍可执行 hi。没有修改两个预置用户程序来伪造并发。

**问：lab5 要改哪里？**
COW 要接入 fork、copyout 和 usertrap 的写缺页路径，维护共享页与引用计数；LAZY 要区分合法堆内缺页与真正越界。当前 VM_NEW 只是短暂事务标记，返回前已清除，不是未来 COW 状态。个人 `LAB5_REFCNT_SCHEME=2` 后续仍须按课程规范实现。

## 7 故障现象与日志定位

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
timeout 3s qemu-system-riscv64 -machine virt -bios none \
  -kernel kernel/kernel -nographic -d int -D /tmp/lab3-int.log \
  > /tmp/lab3-console.log 2>&1
grep 'async:0' /tmp/lab3-int.log | grep -v 'cause:0000000000000008' | head
riscv64-unknown-elf-nm -n kernel/kernel | grep -E 'trampoline|uservec|userret'
# 把实际日志中的 epc 代入，不能把跳板高 VA 直接当物理地址：
# PA = trampoline页物理地址 + (epc - TRAMPOLINE)
# riscv64-unknown-elf-addr2line -e kernel/kernel <换算后的PA>
~~~

| 症状 | 首先核对什么 | 当前源码位置 |
| --- | --- | --- |
| 启用 satp 后黑屏 | RAM、UART、PLIC 是否映射；RAM 是否到 PHYSTOP；X 是否存在 | `vm.c:vm_init` |
| 跳板附近取指缺页风暴 | uservec/userret 是否在已映射的一页内；高地址间接调用是否正确 | Makefile 跳板规则、`trap.c:trampoline_address` |
| 内核中断误入 uservec | C 陷入入口是否首先切 stvec 为 kernelvec | `trap.c:usertrap` |
| 写代码页居然成功 | 派生脚本是否页对齐数据；加载器是否去掉代码 W | `tools/embed.py`、`proc.c:proc_exec` |
| guard 访问拖死全内核 | 用户异常分支是否退出当前进程，而不是 wfi 停机 | `trap.c:usertrap` |
| OOM 后少页 / 原空表消失 | 物理页所有权、事务新节点标记、回滚范围 | `vm.c:finish_mapping/uvm_grow` |
| wait 得不到非零状态 | 指针 W/U 校验、父页表 copyout、退出状态传递 | `proc.c:proc_wait/reap/proc_exit` |
| 输入等待后 read 返回 -1 | 重试 ecall 的原 a0 是否被临时返回值覆盖 | `syscall.c:dispatched_epc` |
| spin 运行但 sh 不响应 | 输入等待是否让出机会、UART 是否唤醒、用户时钟是否轮转 | `proc.c:proc_defer/proc_wake_input/proc_yield` |

正常 hi/badecall 会有 cause 8 的同步异常；badaccess/guardtest 则**应当**出现预期的 cause 13/15。不能把所有 async:0 都判作错误，也不能在非法访问测试中要求“缺页数为零”。
