# 材料二：核心数据结构全景快照

> **截面时刻**：`exec` 刚刚执行完毕、echo 的**首条用户指令尚未运行**。
> 要求见 [`docs/lab0-实验说明书.md` §材料二](../../docs/lab0-实验说明书.md#材料二核心数据结构全景快照)。

---

## 快照 1：进程表

| pid | name | state | parent | pagetable | sz | kstack | chan / 等待原因 | ofile[0] | ofile[1] | ofile[2] |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | init | | | | | | | | | |
| 2 | sh | | | | | | | | | |
| 3 | echo | `RUNNABLE`? | sh | | | | | | | |

字段来源：`kernel/proc.h: struct proc`。

- `state` 取值集合：_TODO（从 `enum procstate` 抄全）_
- 此刻 echo 的 `trapframe->epc` 指向哪里？`trapframe->sp` 指向哪里？

_TODO_

---

## 快照 2：echo 的页表（SV39 映射树）

> 必须标出**虚拟地址区间**与 **PTE 权限位**（`V R W X U G A D`）。
> trampoline / trapframe 的边界是验收明确点名的检查项。

```
MAXVA = 1 << 38  (0x4000000000)
```

| 区域 | 虚拟地址区间 | 页数 | PTE 权限 | 物理页来源 | 备注 |
| --- | --- | --- | --- | --- | --- |
| TRAMPOLINE | `MAXVA-PGSIZE` ~ `MAXVA` | 1 | `R X` (无 `U`) | 内核镜像里那一份 | 所有进程共享同一物理页 |
| TRAPFRAME | `TRAMPOLINE-PGSIZE` ~ `TRAMPOLINE` | 1 | `R W` (无 `U`) | 每进程独有 | |
| guard page | | 1 | **`V=0`** | — | 栈溢出立刻触发缺页 |
| 用户栈 | | | `R W U` | | `sp` 初值 = ? |
| 堆区 / sz 上界 | | | | | exec 后 `sz` = ? |
| 数据段 `.data/.bss` | | | `R W U` | | |
| 代码段 `.text` | `0x0` ~ | | `R X U` | | 入口 = ELF `e_entry` |

**三级页表结构**（画出 L2 → L1 → L0 的实际走向，至少画代码段与 trampoline 两条路径）：

_TODO：手绘图 → `assets/pagetable.png`_

- 为什么 trampoline 页**没有** `U` 位却能在用户态执行时被访问到？（提示：访问它时 CPU 已经在什么特权级？）
- guard page 的 PTE 到底是"不存在"还是"存在但无权限"？在 xv6 里怎么实现的？

_TODO_

---

## 快照 3：文件表与 stdout 引用链

```
proc(echo).ofile[1]  ──→  struct file  ──→  struct inode
                            type=?           type=T_DEVICE
                            ref=?            major=CONSOLE(1)
                            readable/writable
                            off=?
                                              ↓
                                       devsw[CONSOLE].write = consolewrite
```

| 层 | 结构 | 关键字段 | 此刻的值 |
| --- | --- | --- | --- |
| 进程 | `p->ofile[1]` | 指针 | |
| 系统文件表 | `ftable.file[i]` | `type` / `ref` / `readable` / `writable` / `ip` / `off` | |
| inode | `itable.inode[j]` | `type` / `ref` / `major` / `minor` / `nlink` | |
| 设备分发 | `devsw[]` | `read` / `write` | |

- `fork` 之后 sh 与 echo 的 `ofile[1]` 指向**同一个** `struct file` 还是各自一份？`ref` 计数因此是多少？
- `exec` 会不会重置 `ofile` 数组？为什么 echo 还能拿到 stdout？

_TODO_

---

## 💭 自主思考批注

> 💭 _TODO_

> 💭 _TODO_
