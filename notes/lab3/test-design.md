# lab3 实现前测试设计

依据 V3.1 / v2-20260929，在核心实现前确定以下断言。

1. **跨页拷贝边界**：sbrk 分配两页，在页边界前 8 字节构造 16 字节字符串，write 输出必须逐字节一致；read 跨页写回后内容相同。copyout 到代码页、guard、TRAPFRAME 或跨越堆末尾必须返回 -1；n=0、非整页增长、收缩及失败后旧数据保持可用。
2. **保护页错误恢复**：从当前用户 sp 推导栈底，在其下方 guard 读取/写入。子进程应以非零状态被回收；父进程与 Shell 继续工作。
3. **内核映射事务**：保留少量空闲页后发起跨越多个 L1 边界的映射，强制中途 OOM；叶映射与新中间表全部消失，旧映射不变，空闲页数恢复。独立验证 walk 自身失败回滚与重复映射。
4. **内存配额与分配次序**：按 bitmap 中当前空闲页的地址排序，以下中位数作为中点；初始候选区从 PGROUNDUP(kernel_end) 起、PHYSTOP 前保留 DELTA 页。验证逐次分配地址、4KiB 对齐、清零、回收守恒和耗尽 NULL。
5. **进程事务**：fork 保持代码权限、跳过 guard，父子数据互不污染；OOM fork/exec 失败保留原映射；wait 正确 copyout 退出状态；不合法 wait 指针不消费子进程。
6. **官方与回归**：badaccess TEST-3a/3b、memcap TEST-4、dump ABI；lab2 官方 smoke/bufstorm、原自测与 lab1 Banner 前三行逐字节比较。

上述条目在本文件建立时均为待测；运行结果记录于 test-plan.md。
