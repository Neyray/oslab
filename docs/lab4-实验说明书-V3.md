# lab4 实验说明书 V3

来源：课程原件 lab4-实验说明书-学生版-V3.docx，正文逐段提取；表格按单元格顺序展开。

实验 4：进程上下文切换与调度策略
V3 版本（2026-10-09）。配套增量包：lab4-v2-20261009。
lab2 与 lab3 结束时，你的内核已经能 fork 出子进程、在 wait 和控制台读取处让出 CPU，spin 与 sh 可以并发运行。但这套机制是简化版本：多数同学使用一把全局进程表锁、一个静态的调度器上下文，调度顺序固定，时钟中断也未必触发抢占。本实验在此基础上重构：改为每进程一把锁，由时钟中断驱动抢占，把休眠与唤醒整理为标准的 sleep(chan, lk) / wakeup(chan)，最后按学号挂上你的个人调度策略。
你在 lab0 任务三绘制的时钟中断时序图对应本实验的完整流程：“用户态断点 → 内核 → 调度器 → 内核 → 回到断点”，本实验要求每个阶段都能在代码中找到对应位置。
本实验的验收基线为单核（-smp 1）。可自愿挑战双核（-smp 2），稳定通过额外加 3 分（政策见《学生须知》）。
重构前请先执行 git tag lab4-start 标记基线。若重构过程中 lab3 已有功能出现异常，可提交当前状态后执行 git bisect start HEAD lab3-submit 二分定位引入缺陷的提交；调试受阻时请先对照第 3 节排查表自查，再携带日志求助。
0. 开始之前
0.1 合并增量包
按《合并说明》解压后，依次完成：
覆盖 kernel/swtch.S（增量包预置，只读）。它保存与恢复 14 个被调用者保存寄存器（callee-saved：ra、sp、s0–s11），偏移依次为 0、8、16 …… 104 字节。若你在 lab2 自行编写过 swtch.S，先用 git diff 对比；覆盖后检查 struct context 的字段顺序与上述偏移一一对应，否则切换后寄存器错位。
覆盖 kernel/syscall.h、user/user.h、user/usys.pl：新增 SYS_schedstat = 23、SYS_time_rdtime = 24、SYS_setickets = 25。增量包只提供用户侧桩代码，三个系统调用的内核实现由你完成（阶段四）。
接入测试程序：把 support/tests/ 下的 common.h、common.c、bench.c、lottery_test.c、stride_test.c 拷贝到 user/，在 Makefile 的 UPROGS 定义之后加入 gifts/Makefile.upgrade 中的 UPROGS += … 一行。lab3 的构建规则在 user/common.c 存在时会自动链接它。
通读 support/tests/README-tests.md（前置能力清单、各测试的适用策略与判据）和 support/schedstat.patch（三个系统调用的语义）。
执行 make qemu，确认 lab3 功能（sh、hi、badaccess、dump_pagetable）仍正常，再提交并打 lab4-start 标签。此时测试程序可以编译，但尚不能通过。
0.2 起点自查：你的 lab3 内核与本实验目标的差距
动手前对照下表，在设计笔记中逐行写明你当前的实现方式。
项目
lab3 结束时常见的实现
本实验目标
进程表的锁
一把全局锁（如 ptlock）保护全部进程
每个进程一把 p->lock；父子关系另用一把全局 wait_lock 保护
调度器上下文
一个静态变量（如 sched_ctx）
每个 CPU 一个 struct cpu，内含调度器 context、当前进程指针 proc、关中断嵌套深度 noff 与进入前的中断状态 intena
自旋锁的嵌套计数
spinlock.c 内的静态变量
放进 struct cpu，并提供 holding(lk) 判断当前 CPU 是否持有某锁
让出 CPU 的时机
只在 wait、控制台读取处主动让出
另加时钟中断抢占（yield()）
调度顺序
固定顺序扫描进程表
先实现轮转，再替换为你的策略
休眠与唤醒
一个只支持全局锁的 sleep 或忙等
sleep(chan, lk) 可配合任意一把锁使用
若你的 lab2/lab3 只实现了忙等，没有执行流切换，请先读完阶段一再动手，工作量会比表中多一些。
0.3 本实验需要新增的系统调用
课程测试依赖下列系统调用。本实验不要求实现 pipe。
系统调用
调用号
最小语义
在哪个阶段实现
pause(n)
13
当前进程休眠 n 个时钟 tick 后返回
阶段三
kill(pid)
6
给目标进程置”已终止”标志；可选，测试不强制依赖
阶段三
schedstat(pid, &cpu, &slice)
23
查询目标进程累计 CPU tick 与最近一次连续运行的 tick 数
阶段四
time_rdtime()
24
返回 time CSR 计数（内核中 r_time()），不是 tick
阶段四
setickets(n)
25
设置当前进程票数；n < 1 返回 -1
阶段四
另外，wait(&st) 必须把子进程的退出码写回 st，bench 会核对退出码。
0.4 三组必须分清的概念
trapframe 与 context 是两种现场。 trapframe 用于特权级切换，保存用户态全部 31 个通用寄存器，由 trampoline.S 搬运；context 用于内核态执行流之间的切换，由 swtch.S 搬运，只保存 14 个寄存器。为什么 swtch 可以少保存？少保存的是哪部分、依据是什么？这是思考题 1，也是设计笔记第一题，请在读懂 swtch.S 之后回答。
进程状态机。 全局进程表 proc[NPROC] 中，每个进程处于 UNUSED / USED / RUNNABLE / RUNNING / SLEEPING / ZOMBIE 之一。动手前画一张状态迁移图，在每个箭头上标注触发它的函数，以及修改状态时持有的锁。
机制与策略分离。 寄存器保存、栈切换、让出与唤醒属于机制，任何调度算法都要使用；从就绪进程中选出下一个属于策略。先完成机制（阶段一至三）并用轮转调度验证，再替换策略（阶段四）。
0.5 调度器栈
进程 A 切换到进程 B 时，中间要经过调度器自己的栈，它不属于任何进程。为什么调度器不能直接在 A 的内核栈上把 A 换出？考虑 A 正在 exit() 的情形：A 的内核栈随后会被回收。lab0 思考题 3 你已写过结论，本实验要在代码中落实。
0.6 你的调度策略
LAB4_POLICY_ID = 学号 % 10，票数参数为 7 + 学号 % 13。
【重要】 基线 kernel/course_sid.h 中 LAB4_POLICY_ID 的注释写着”0=MLFQ 1=彩票 2=步长 3=优先级…“，这是早期占位文字，与下表不符。宏的数值正确，策略含义以下表为准。
ID
策略族
具体要求
可观测特征（验收依据）
0
多级反馈队列（Multi-Level Feedback Queue, MLFQ）
2 级队列，最高级时间片见下方 tick 口径，下一级 ×2
schedstat 的 last_slice_ticks 呈两档分布
1
MLFQ
3 级队列，时间片逐级 ×2
三档分布
2
MLFQ
4 级队列，时间片逐级 ×2
四档分布
3
彩票调度（Lottery）
静态默认票数 = 7 + 学号 % 13
长窗口内 CPU 占比与票数比近似相等
4
彩票调度
动态：进程每主动让出 1 次加 1 票，上限 32
短窗口占比随运行时间收敛
5
彩票调度
比例共享：按进程名分配默认票数 hi=1 / spin=2 / sh=4
CPU 占比近似 1:2:4
6
步长调度（Stride）
stride = 1000 / tickets，默认 tickets = 7 + 学号 % 13
占比与票数成正比，且分配均匀
7
步长调度
默认票数按进程名分配，规则同 ID 5
同上（1:2:4）
8
优先级调度
4 级静态优先级，数值大者优先
高优先级独占运行，低优先级等待边界清晰
9
优先级调度
2 级优先级 + 老化：每等待 8 个 tick 提升 1 级
低优先级进程最终获得 CPU（无饥饿）
补充约定：
tick 口径：tick 指一次时钟中断。若你在 lab2 中把 LAB2_TICK 实现为”每个时间片包含的中断次数”，ID 0–2 的最高级时间片即 LAB2_TICK 个 tick；若实现为”时钟中断周期的倍数”（stimecmp = time + LAB2_TICK × 基本周期），最高级时间片取 1 个 tick。两种口径均可，须在设计笔记中写明所用口径。
setickets 覆盖默认票数：ID 3 的静态默认值、ID 5/7 按进程名分配的默认值，都会被 setickets 的设定值覆盖（课程测试据此设票）。ID 0–2、8–9 的内核可以只记录该值、不参与调度，但仍须返回 0。
1. 分阶段任务指引
阶段一：锁与上下文结构重构
目标：引入 struct cpu 与每进程锁，sched() / yield() 在不变式检查下完成切换；lab3 已有功能全部保持正常。
【提示】 头文件包含顺序：在 proc.h 的 struct proc 内直接按值定义 struct spinlock lock; 时，需确保 proc.h 顶部已包含定义 struct spinlock 的头文件（如 #include "defs.h" 或独立的 spinlock.h）。若未包含，先引入 proc.h 的源文件会触发 field 'lock' has incomplete type 编译错误。
先读懂 swtch.S，在设计笔记中回答：
两个参数按调用约定位于哪两个寄存器？
保存哪些寄存器、恢复哪些？ra 在这里起什么作用？恢复 ra 后执行 ret，CPU 跳到哪里？
调用者保存（caller-saved）寄存器为什么不需要 swtch 处理？
一个从未运行过的新进程，它的 context.ra 和 context.sp 应在何处、设为什么值？（提示：allocproc 中设置，ra 指向一个”首次被调度时执行”的函数，常命名为 forkret。）
再写 sched()。进入 sched() 时必须同时满足以下四条不变式，建议逐条用 panic 检查：
持有当前进程的 p->lock（用 holding() 判断）；
除 p->lock 外不持有任何其他自旋锁（当前 CPU 的 noff == 1）。原因与阶段三的丢失唤醒有关：若还持有别的锁就切走，其他进程获取该锁时会一直自旋；
进程状态已改为 RUNNABLE、SLEEPING 或 ZOMBIE，不能仍是 RUNNING；
中断已关闭。
sched() 在调用 swtch 前保存 mycpu()->intena、返回后恢复。原因是 intena 记录的是”这个进程”关中断前的中断状态，切换期间 CPU 会执行别的进程，可能改写该值。
yield() 的职责：获取 p->lock、把状态改为 RUNNABLE、调用 sched()、释放 p->lock。
【警告】 锁在两个执行流之间交接（关键易错点）：
为什么不能在调用 swtch 之前释放 p->lock？若进程把状态改为 RUNNABLE 后立即释放 p->lock，在它尚未执行完 swtch 离开当前内核栈时，其他执行流就可能选中该进程并切入同一内核栈，导致两个执行流共用同一个栈而崩溃。因此，p->lock 必须跨越 swtch 保护”修改状态 + 切换内核栈”的完整过程；
进程在 yield() 中获取 p->lock 后切走，这把锁由调度器在 swtch 返回后释放；
调度器选中进程时获取 p->lock 并切入，这把锁由进程在 sched() 返回后（yield() 末尾）释放；
新进程第一次被调度时不经过 sched()，而是直接从 forkret 开始执行，因此 forkret 的第一件事就是释放 p->lock。若你在 lab2/lab3 的 forkret 中释放的是全局锁，改为每进程锁后要同步修改，否则第一次 fork 之后就会出现锁重入 panic 或死锁。
【重要】 阶段一验证的前置条件（同步更新既有 sched() 调用点）： 启动 sh 读取串口命令会调用 consoleread -> sleep -> sched，运行 hi 会调用 kwait -> sleep -> sched 与 kexit -> sched。一旦在 sched() 入口启用上述四条 panic 断言，所有调用 sched() 的路径都必须满足”仅持有当前进程 p->lock“的不变式。因此，在运行 make qemu 验证阶段一之前，请先对照阶段三的锁序规则，把已有 sleep、wakeup、wait、exit 中的旧全局锁一并替换为 p->lock 与 wait_lock 配对，确保进入 sched() 时已持有 p->lock 且已释放其他自旋锁。
验证：完成锁与结构替换后（调度器仍用简单轮转），make qemu 进入 sh，hi、badaccess、dump_pagetable 均正常。此时尚未加入时钟抢占，行为应与 lab3 相同。
阶段二：调度器主循环与时钟抢占
目标：scheduler() 主循环正常运行，时钟中断触发抢占，多个计算密集型进程交替推进。
设计问题：
主循环每一轮做什么？扫描进程表前要短暂开中断，原因是什么？（提示：所有进程都在休眠时，只有中断能唤醒它们；若调度器一直关中断扫描，系统将无法继续运行。）
选中 RUNNABLE 进程后：改状态为 RUNNING、记录 c->proc、swtch 切入。从 swtch 返回意味着该进程已让出 CPU（时间片用完、休眠或退出），此时它的状态已由它自己改好，调度器只需清空 c->proc 并释放 p->lock。
一轮扫描下来没有就绪进程时，执行 wfi（Wait For Interrupt，等待中断）让 CPU 暂停到下一次中断。
抢占点放在哪里？在 usertrap() 的时钟中断分支中调用 yield()。是每次时钟中断都让出，还是累计满一个时间片再让出？按 0.6 节的 tick 口径决定。
【警示】 若你还打算在内核态时钟中断（kerneltrap()）中调用 yield()，必须在调用前把 sepc 与 sstatus 保存到局部变量、yield() 返回后写回。切换期间其他进程会改写这两个 CSR，不恢复会导致返回到错误地址。只在 usertrap() 中抢占也满足本实验要求。
验证：预置 sh 不支持后台运行（&），且 spin 不会退出，无法用”后台跑 spin、同时敲命令”的方式验证。请自行编写一个小程序（加入 UPROGS），例如：
// user/alt.c：两个子进程分别持续打印 A、B#include "kernel/types.h"#include "user/user.h"static void run(char c) {  for (int i = 0; i < 40; i++) {    for (volatile int d = 0; d < 20000000; d++)      ;    write(1, &c, 1);  }  exit(0);}int main(void) {  if (fork() == 0) run('A');  if (fork() == 0) run('B');  wait(0); wait(0);  printf("\nalt done\n");  exit(0);}
加入抢占前，输出为全部 A 之后全部 B（或相反）；加入抢占后，A 与 B 交错出现，最后打印 alt done 并回到提示符。
阶段三：休眠与唤醒，pause 与 kill
目标：在阶段一已完成 p->lock 与 wait_lock 基础锁替换的前提下，推演并手工复现”丢失唤醒（lost wakeup）“窗口，核验 sleep(chan, lk) / wakeup(chan) 与 wait / exit 的无死锁锁序；基于 sleep 实现 pause(n)，可选实现 kill(pid)。
先在设计笔记中推演丢失唤醒：进程 A 检查条件不满足，决定休眠；在”检查完毕”与”正式休眠”之间，进程 B 生产了资源并调用 wakeup。此时 A 尚未处于 SLEEPING，唤醒落空；随后 A 进入休眠，且不会再有人唤醒它。画出时序线，推导 sleep 的设计约束：
“把状态改为 SLEEPING 并让出 CPU”必须与”唤醒方扫描进程表”互斥，否则必然存在丢失唤醒的窗口；
因此 sleep(chan, lk) 的锁序是：调用者持有条件锁 lk 进入 → 先获取 p->lock → 再释放 lk → 设置 p->chan、状态改为 SLEEPING → sched() → 醒来后清除 p->chan → 释放 p->lock → 重新获取 lk 再返回；
wakeup 为什么必须持有目标进程的 p->lock 去改状态？
动手复现丢失唤醒（记入设计笔记）：在实现 pause(n) 后，临时把 sleep(chan, lk) 改为”先 release(lk)、插入 for (volatile int i = 0; i < 1000000; i++); 空转延迟、再 acquire(&p->lock)“，运行 bench 观察父进程在并发子进程阶段永久阻塞的现象；记录复现现象与原因后，再恢复”先 acquire(&p->lock)、再 release(lk)“的正确顺序。
易错点：
wakeup(chan) 遍历进程表时跳过当前进程，否则会对自己已持有的 p->lock 再次获取，造成死锁；
exit() 调用 sched() 后不再返回；c->proc 由调度器在 swtch 返回后清空，exit() 中不要提前清空，否则 sched() 中的 myproc() 返回空指针；
wait() 与 exit() 涉及父子两个进程，若各自持有对方的 p->lock 容易形成死锁。建议用一把全局 wait_lock 保护”父子关系与 ZOMBIE 检查”：wait() 持 wait_lock 扫描子进程，无 ZOMBIE 时调用 sleep(p, &wait_lock)；exit() 持 wait_lock 唤醒父进程并把自己改为 ZOMBIE。锁的获取顺序固定为先 wait_lock 后 p->lock。
pause(n)：内核维护全局 tick 计数 ticks 及其锁 tickslock；时钟中断处理中 ticks++ 并 wakeup(&ticks)。sys_pause 记下起始值，循环 sleep(&ticks, &tickslock) 直到经过 n 个 tick。（注意：用户库把它命名为 pause，内核中的 sleep(chan, lk) 是另一个函数。）
kill(pid)（可选）：只给目标进程置 killed 标志；若目标正在 SLEEPING，可把它改为 RUNNABLE，让它醒来后自行检查标志。目标进程在返回用户态前检查标志并调用 exit。思考：为什么不能在 kill 中直接释放目标进程的资源？
验证：运行 bench。它先做 16 轮”fork → 子进程立即 exit → 父进程 wait“，再并发创建 8 个子进程调用 pause(5) 并逐个回收。通过时输出两行 RESULT 并以 bench: PASS 结束。若停在第一部分或并发阶段没有输出，说明 wait/exit 或 pause 之间存在丢失唤醒。
阶段四：观测接口与你的调度策略
目标：实现 schedstat、time_rdtime、setickets 三个系统调用；按 0.6 节实现个人策略，并通过观测数据证明行为正确。
先实现观测接口（规格见 support/schedstat.patch）：
struct proc 中加入 cpu_ticks、last_slice_ticks、tickets（MLFQ / 优先级策略可一并加入 mlfq_level 等字段）；
在 usertrap() 时钟分支中、让出之前，对当前进程 cpu_ticks++、last_slice_ticks++（只统计从用户态进入的时钟中断）；
调度器选中进程、执行 swtch 之前，把 last_slice_ticks 清零；
sys_schedstat 查进程表时持 p->lock 读取两个计数，释放锁后再用 copyout 写回用户指针（copyout 可能访问用户页表，不宜在持有进程锁时调用）。
设计问题：
策略所需的状态（优先级、票数、pass、已用 tick 等）放在 struct proc 中还是单独的调度表？由谁初始化？fork 出的子进程继承父进程的值还是取默认值？
MLFQ 的降级判定放在时钟中断处理中还是调度器中？“主动让出”（休眠）与”时间片用完”如何区分？（注意：MLFQ 下 usertrap() 仅在 p->last_slice_ticks >= 当前层级时间片 时才调用 yield() 并降级；若沿用阶段二”每拍无条件 yield()“，进程每跑 1 个 tick 就会返回 scheduler() 被清零 last_slice_ticks = 0，永远无法观测到长于 1 tick 的时间片。）
当把 scheduler() 改为”两步法”（先扫描 proc[] 选出最高优先级或最小 pass 的就绪进程，再执行切换）时，若多个就绪进程处于同一优先级队列，如何避免每次固定从 proc[0] 扫描导致低槽位进程独占 CPU？（提示：维护轮转起始下标 rr_idx，每轮从 (rr_idx + 1) % NPROC 开始循环扫描。）
内核中没有标准库，彩票调度的随机数从哪里来？（可用线性同余发生器：x = x * a + c，取高位作为结果；种子可用 r_time()。）就绪进程总票数为 0 时如何处理？
步长调度中，新创建或刚被唤醒的进程 pass 值应如何设定，才不会长时间独占 CPU？
验证：按策略族选择测试（判据详见 README-tests.md）。
你的 ID
运行
通过标准
全部
bench
bench: PASS
3、4、5
lottery_test
输出 RESULT lottery_ratio = a:b，默认票数 60:20 时 a 的占比在 60%–90% 之间
6、7
stride_test
输出 RESULT stride_ratio = a:b，b/a 在 2.4–3.6 之间，且每个窗口两者都获得 CPU
0–2、8–9
自写观测程序
见下方说明
MLFQ 与优先级策略未提供通用策略测试程序，请编写观测程序（可参考 lottery_test.c 的结构，包含两个独立用例）：
MLFQ（ID 0–2）：
用例一（时间片逐级倍增验证）：观测纯计算子进程在各层级完成的 last_slice_ticks 档位（如 ID 1 应观测到 1, 2, 4）。关键提醒：由于 scheduler() 每次切入进程前都会将 p->last_slice_ticks 清零，若系统中仅有 1 个纯计算子进程就绪，当父进程调用 pause(k)（）休眠期间，该子进程耗尽高层级时间片（1 或 2 ticks）并调用 yield() 后会被 scheduler() 立即再次选中切入并清零 last_slice_ticks，导致父进程醒来读不到 1 和 2。编写测试时，建议同时创建 1 至 2 个伴随计算子进程（使目标子进程让出 CPU 后停留在 RUNNABLE 状态等待下一轮调度，保留刚完成的 last_slice_ticks 峰值供唤醒的父进程读取），或以 pause(1) 逐拍高频采样记录各档位是否出现。
用例二（交互进程高优先级保持验证）：让一个已经降入最低优先级队列的后台纯计算子进程持续运行，同时创建一个每轮工作后主动调用 pause(1) 让出 CPU 的交互型子进程，通过 schedstat 验证交互型子进程因未耗尽时间片而始终保持在最高优先级队列并能及时抢占后台计算进程。
优先级（ID 8–9）：创建高、低优先级各一个纯计算子进程（优先级如何设置由你设计，可复用 setickets 的参数或新增接口，须在设计笔记中说明），比较两者的 cpu_ticks。ID 8 应看到低优先级进程几乎得不到 CPU；ID 9 应看到低优先级进程在等待一段时间后获得 CPU。
lottery_test 约需 260 个 tick，stride_test 约需 360 个 tick；时钟周期较长时可达数分钟，属正常现象。
挑战任务：双核对称多处理（-smp 2）导学与跨越清单（选做，加 3 分）
单核（-smp 1）通过全部测试后，若希望挑战双核（-smp 2），核心目标是让两颗硬件线程（Hart 0 与 Hart 1）拥有各自独立的 struct cpu、同时运行 scheduler() 并靠自旋锁安全共享进程表。从单核基线跨越到双核运行，需依次排查并补齐以下 7 处软硬件衔接要点：
启动栈按核划分与副核放行（kernel/entry.S）： lab1 的 entry.S 仅分配了单核栈空间，且将 mhartid != 0 的硬件线程拦截在死循环中。双核下须将 boot_stack 扩充为 NCPU 份，按 sp = boot_stack + (mhartid + 1) * StackSize 为每个核设置独立栈顶后再放行副核进入 start()，否则主副核会共用同一栈顶互相覆写栈帧。
tp 寄存器在用户态往返与内核中断中的保护（start.c / trap.c / trapvec.S）：
在 start.c 执行 mret 进入监管者态（S 态）前，调用 w_tp(r_mhartid()) 将硬件线程编号写入 tp 寄存器，使 cpuid() 与 mycpu()（须在关中断下调用）能够据此索引 cpus[cpuid()]。
最隐蔽易错点：trampoline.S 的 userret 返回用户态时会用用户寄存器 tf->tp 覆盖硬件 tp，而下次从用户态陷入 uservec 时会执行 ld tp, 32(a0) 从 tf->kernel_hartid 恢复内核 tp。若沿用 lab3 在 usertrapret() 中写死的 tf->kernel_hartid = 0，副核（Hart 1）首次运行用户进程并陷入内核时，tp 就会被篡改为 0，导致双核共用 cpus[0] 崩溃。因此 usertrapret() 必须改为 tf->kernel_hartid = r_tp()。
同时检查 trapvec.S 的 kernelvec：保存与恢复寄存器时不要用栈上旧值覆盖 tp（或确保不在内核态中断中跨核迁移），以免进程跨核迁移后把旧核编号写回新核的 tp。
主核全局初始化与副核每核初始化的同步屏障（kernel/main.c）： 定义全局标志 volatile int started = 0;。主核（cpuid() == 0）负责打印 lab1 启动横幅、初始化物理内存、内核页表、进程表、控制台、中断控制器及首个用户进程，完成后执行 __sync_synchronize() 并置 started = 1；副核（cpuid() != 0）循环等待 started != 0 后，仅执行本核硬件初始化（w_stvec、kvminithart、plicinithart、timerinit）即进入 scheduler()，严禁二次打印横幅或重复初始化全局表。
自旋锁归属核记录与 holding(lk) 跨核判定（kernel/spinlock.c）： 单核下 holding(lk) 仅检查 lk->locked 即可工作；但双核下当主核持有锁、副核调用 acquire(lk) 正常自旋等待时，若不检查持锁核身份，副核会因 lk->locked != 0 误判为”当前核重入加锁”而触发 panic("acquire")。必须在 struct spinlock 中加入 struct cpu *cpu; 字段，加锁时记录 lk->cpu = mycpu()，解锁前清零 lk->cpu = 0，并将 holding(lk) 定义为 lk->locked && lk->cpu == mycpu()。
外设中断控制器（PLIC）上下文与全局 ticks 职责划分（kernel/trap.c）： 将 plicinithart()、plic_claim()、plic_complete() 中硬编码的上下文参数 0 改为 cpuid()；由于两颗核心各自产生时钟中断，全局 ticks 的自增与 wakeup(&ticks) 应限定仅由主核（cpuid() == 0）在持有 tickslock 下执行，防止睡眠计时速率翻倍。
Makefile 的 -smp $(CPUS) 参数核对： 检查 Makefile 中是否定义了 CPUS ?= 1 并在 QEMUOPTS 中传入 -smp $(CPUS)。若缺失该参数，执行 make qemu CPUS=2 时 QEMU 仍只启动 1 颗核心，会造成”双核已通过”的误判。
双核下的测试范围说明： lottery_test 与 stride_test 均只创建 2 个纯计算子进程（期望时间片比为 3:1 或 1:3）。在双核（CPUS=2）工作保守调度器中，2 个就绪进程会分别在主核与副核上同时满载运行；由于单线程进程同一时刻只能在 1 颗核心上执行，单个进程最多占据双核总时间片的 50%，两进程的累计 cpu_ticks 比例在双核下恒为 1:1。因此，lottery_test 与 stride_test 的份额比例验证须在单核（CPUS=1）下执行；双核（CPUS=2）挑战使用 bench、alt 及历史回归测试检验多核并发同步的正确性。
2. 思考题
思考题不收书面答案，但验收问答直接从这里抽选。
swtch 只保存 14 个寄存器，且不显式保存 PC，为什么仍能正确切换回来？完整保存 31 个通用寄存器是否可行，代价是什么？
从进程 A 的用户态切到进程 B 的用户态，依次经过哪些栈？每个切换点由哪段代码（trampoline.S / swtch.S / 调度器）完成？
如果 sleep 在未获取 p->lock 的情况下先释放外部锁、再修改状态，构造一个具体的交错序列，使该进程永久阻塞。
3. 排查思路
先按《学生无 gdb 调试手册》获取日志，再对照下表定位。
症状
排查方向
一调用 sched() 就 panic
四条不变式逐条核对：是否持有 p->lock、是否还持有其他锁、状态是否已改、中断是否关闭
阶段一刚加完 sched() 四条断言，启动 sh 前即触发 panic("sched p->lock") 或 panic("sched locks")
sh 启动读取串口会调用 sleep -> sched：检查 sleep、wait、exit 是否已同步改为在调用 sched() 前获取 p->lock 并释放其他自旋锁（见阶段一重要提示框）
第一次 fork 后即锁重入 panic 或系统停止响应
forkret 是否释放了 p->lock（阶段一警告框）
系统运行一段时间后整体停止，时钟不再推进
调度器空闲扫描时中断是否开过；wfi 前是否可能永久关中断
exit 之后内核崩溃，myproc() 为空
c->proc 是否在 exit 中被提前清空
加入内核态抢占后，内核在异常地址崩溃
kerneltrap() 调用 yield() 前后是否保存并恢复了 sepc/sstatus
alt 输出不交错
时钟中断是否到达 usertrap()；时钟分支是否调用了 yield()
bench 停在第一部分或并发阶段无输出
wait/exit 或 pause 之间的丢失唤醒：sleep 的锁序是否正确，exit 是否持 wait_lock 唤醒父进程
bench 报”退出码异常”
wait(&st) 是否把子进程退出码写回用户空间
测试报”总 CPU 采样过少”
pause 是否真的按 tick 休眠；cpu_ticks 是否在 usertrap() 时钟分支中累加
测试报”schedstat 采样失败”
schedstat 是否已实现；pid = -1 与查询其他进程两种情况是否都处理
MLFQ 观测程序中 last_slice_ticks 始终不超过 1，或读不到高层级短时间片（如 1、2）
检查 usertrap() 是否仅在 last_slice_ticks >= 当前层级时间片 时才调用 yield()；检查是否因单子进程让出后被 scheduler() 立即重调度清零（建议增加伴随计算进程）
阶段四改为两步法选择进程后，同优先级下仅一个进程运行、其余进程饥饿
检查 scheduler() 扫描 proc[] 时是否每次固定从下标 0 开始，改为维护轮转游标 rr_idx
lottery_test 占比接近 50:50
单核下检查 setickets 是否生效、调度器是否仍是轮转；若在 CPUS=2 下运行属正常现象（双核两进程各占一核），请切回 CPUS=1 验证份额比例
双核（CPUS=2）启动打印两次横幅，或在 acquire / sched 处立即 panic
对照阶段四”双核挑战跨越清单”核查：entry.S 分核栈、main.c 的 started 屏障、usertrapret() 的 tf->kernel_hartid = r_tp()、holding(lk) 的 lk->cpu == mycpu()
进程退出后空闲物理页数量对不上
exit 与 wait 的交接：谁置 ZOMBIE、谁调用 freeproc；内核栈、页表、trapframe 是否逐项释放
4. 交付与验收
自测用例：动手前按策略族编写两个用例并归档备查。示例：彩票/步长——两个纯计算进程的 CPU 比例，以及 setickets 改票后比例随之变化；MLFQ——纯计算进程的片长档位变化，以及频繁休眠的进程保持在高优先级；优先级——高、低优先级进程的 CPU 占用边界，以及老化前后低优先级进程的等待时长。
回归：lab2 的 inject_uart.py 与 lab3 的 dump_pagetable 各一条，均须通过。
归档：
git add -Agit commit -m "feat(lab4): context switch and scheduling policy"git tag lab4-submitgit archive --format=zip -o 提交-lab4-<学号>.zip lab4-submit
验收内容：现场运行 bench 与适用于你策略的测试（或自写观测程序），解释观测数据；回答三个问题（重点抽选思考题、调度器栈、丢失唤醒）。双核挑战单独测试。
可选加分与跨章知识对照回顾：若在实现进程调度后修正了 lab0 时钟中断与调度器栈时序图，现场出示带修正批注的图纸，可印证并完善早期设计记录，并作为加分参考。