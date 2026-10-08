# lab3 三级页表与物理内存管理

目标：把 lab2 的静态过渡内存改造成动态物理页管理与独立 SV39 地址空间；用逐页受控拷贝保护内核，完成 sbrk、fork/exec 页表化与非法访问隔离。

| 文档 | 内容 |
| --- | --- |
| [design.md](design.md) | 参数、控制流、3 道原题、分配器与回滚取舍、现场短答、追问、故障定位 |
| [test-plan.md](test-plan.md) | 可复制的验收命令；官方、边界、OOM、回归测试与实测结果 |
| [test-design.md](test-design.md) | 核心实现前确定的边界与错误路径断言 |
| [实验说明书 V3.1](../../docs/lab3-实验说明书-V3.1.md) | 最新任务书的文本转换，原件保留在 materials |
| [tests/lab3_selftest.py](../../labs/2024302141121-kernel/tests/lab3_selftest.py) | 一次执行官方测试、UART 回归、用户边界测试与临时内核压力测试 |

## 个人参数与自定常量

| 名称 | 值 | 作用 |
| --- | ---: | --- |
| `LAB3_ALLOC_ORDER` | 2 | 当前空闲地址集合的下中位数分配 |
| `LAB3_GUARD_PAGES` | 2 | 数据区与用户栈之间两页无映射 |
| `LAB3_MEMCAP_DELTA` | 1 | PHYSTOP 前一页不进入分配器 |
| `PGSIZE` | 4 KiB | 页表、用户页与物理分配粒度 |
| `USERSTACK` | 1 页 | 用户栈大小 |
| 进程槽数 / 内核栈 | 8 / 8 KiB | 单核过渡执行框架，内核栈静态恒等映射 |

## 现场验收流程

1. 构建：`make clean && make`，核对原 Banner、dump 和 `sh>`。
2. 观测：启动自动 dump；在 sh 等待输入时按 Ctrl-P 重放，解释 RX、RW、guard、trapframe 与 trampoline。
3. 官方：执行 `badaccess`、`memcap`，确认 TEST-3a/3b/4 PASS 后仍回到 Shell。
4. 边界与恢复：`guardtest`、`vmtest`、`oomtest`，验证跨页、退出状态与 OOM 回滚。
5. 并发：`concurrent` 启动预置 spin + sh，继续输入 `hi`；结束按 Ctrl-a 松开再按 x。
6. 回归：lab2 的 smoke/bufstorm 和 13 项自测；lab1 Banner 与 23 项自测。
7. 三题问答：原题 + 分配器顺序、映射回滚、资源所有权、进程生命周期。

## 自测用例（动手前设计，现场可解释）

| 用例 | 程序/位置 | 检查内容 | 实测 |
| --- | --- | --- | --- |
| T3-1 跨页边界 | `user/vmtest.c` | 跨物理页 write/read；只读、越界、溢出指针；字节级 sbrk；父子隔离；wait 状态 | PASS |
| T3-2 保护页恢复 | `user/guardtest.c` | 两个 guard 分别读/写，4 个子进程均被终止，父进程正常回收 | PASS |
| T3-3 映射事务 | `kernel/mmtest.c` | 先消耗到个位数空闲页，强制 walk/mappages/增长失败；旧结构、数据与计数恢复 | PASS |
| T3-4 配额与回收 | `user/oomtest.c` | 耗尽后 fork/exec 失败；部分新空间失败；归还少量页可原样重新申请；最终可再 fork | PASS |
| T3-5 并发 | `user/schedtest.c`、`user/concurrent.c` | 子进程在父 wait 前执行；定时唤醒；预置 spin 与 sh 同时前进 | PASS |

运行：

~~~bash
cd ~/projects/oslab/labs/2024302141121-kernel
python3 tests/lab3_selftest.py --all-orders
~~~

2026-10-08 实测 25 项全部通过。默认个人参数下 Shell 空闲页统计在所有用户测试前后保持 `32650 → 32650`；memcap 为 2036 个 64 KiB 块后返回 OOM。页数会随内核镜像大小变化，应检查守恒和错误语义，不能把该快照数值作为跨版本固定答案。

## 完成状态

- [x] 增量包 v2 原样单独提交（`a2570d0`），`lab3-start` 指向该导入提交
- [x] 物理页位图、计数索引、中点分配、清零、配额和回收
- [x] 动态三级页表、映射/增长事务、失败回滚与拆除
- [x] 内核 RAM 到 PHYSTOP 的恒等映射、外设与同址跳板
- [x] RX 代码、RW 数据、用户栈、两页 guard、trapframe/trampoline 权限
- [x] 受控 copyin/copyout/copyinstr，系统调用用户指针审查
- [x] EAGER sbrk；LAZY 请求暂按 EAGER 处理
- [x] fork 深拷贝与 exec 成功后提交，失败保留旧空间
- [x] 用户错误安全终止、wait 状态回传、进程资源回收
- [x] dump ABI、Ctrl-P 重放与单核 spin/sh 并发
- [x] 官方测试、两组任务书自测、OOM 回滚与早期实验回归
- [ ] 本人复核思考底稿、形成口述解释；在 lab0 手绘图上用不同颜色标注实际差异

本轮保持单核；完整内核续体调度、个性化 lab4 策略、COW、LAZY 缺页分配与文件系统属于后续实验。
