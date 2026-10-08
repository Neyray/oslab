#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "param.h"
#include "proc.h"
#include "defs.h"
#include "mm.h"

extern char trampoline[];
struct embedded_program {
  uint64 start, end, entry, readonly_end, memory_end;
  char name[32];
};
extern const struct embedded_program _uprog_table[];
static uint8 kernel_stacks[LAB2_NPROC][LAB2_KSTACK_SIZE]
  __attribute__((aligned(PGSIZE)));
static struct proc processes[LAB2_NPROC];
static struct proc *current;
static int next_pid = 1;
typedef char trapframe_a0_offset_must_match[
  __builtin_offsetof(struct trapframe, a0) == 112 ? 1 : -1];
typedef char trapframe_t6_offset_must_match[
  __builtin_offsetof(struct trapframe, t6) == 280 ? 1 : -1];
typedef char embedded_table_size_must_match[sizeof(struct embedded_program) == 72 ? 1 : -1];

struct proc *myproc(void) { return current; }

static int
string_equal(const char *a, const char *b)
{
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

static pagetable_t
new_space(struct trapframe *tf)
{
  pagetable_t pt = kalloc();
  if (!pt) return 0;
  if (mappages(pt, TRAMPOLINE, PGSIZE, PGROUNDDOWN((uint64)trampoline),
               PTE_R | PTE_X) < 0 ||
      mappages(pt, TRAPFRAME, PGSIZE, (uint64)tf, PTE_R | PTE_W) < 0) {
    vm_free(pt);
    return 0;
  }
  return pt;
}

static void
release_slot(struct proc *p)
{
  vm_free(p->pagetable);
  if (p->trapframe) kfree(p->trapframe);
  memory_zero(p, sizeof(*p));
}

static struct proc *
allocate_slot(void)
{
  int i;
  for (i = 0; i < LAB2_NPROC; i++) {
    struct proc *p = &processes[i];
    if (p->state != PROC_UNUSED) continue;
    memory_zero(p, sizeof(*p));
    p->trapframe = kalloc();
    if (!p->trapframe) return 0;
    p->pagetable = new_space(p->trapframe);
    if (!p->pagetable) { release_slot(p); return 0; }
    p->kernel_stack_top = (uint64)&kernel_stacks[i][LAB2_KSTACK_SIZE];
    p->pid = next_pid++;
    p->user_satp = MAKE_SATP(p->pagetable);
    return p;
  }
  return 0;
}

static int
within_user(struct proc *p, uint64 va, uint64 n)
{
  return p && va <= p->sz && n <= p->sz - va;
}

int proc_validate(uint64 va, uint64 n, int permission)
{
  return within_user(current, va, n) ? vm_check(current->pagetable, va, n, permission) : -1;
}
void proc_dump_current(void)
{
  if (current && current->pagetable) {
    dump_pagetable(current->pagetable);
    printf("lab3 memory: free=%lu pid=%d brk=%p\n", kfree_count(), current->pid, (void *)current->sz);
  }
}

int proc_copyin(void *dst, uint64 src, uint64 n)
{
  return within_user(current, src, n) ? copyin(current->pagetable, dst, src, n) : -1;
}
int proc_copyout(uint64 dst, const void *src, uint64 n)
{
  return within_user(current, dst, n) ? copyout(current->pagetable, dst, src, n) : -1;
}
int proc_copyinstr(char *dst, uint64 src, uint64 max)
{
  if (!current || src >= current->sz) return -1;
  if (max > current->sz - src) max = current->sz - src;
  return copyinstr(current->pagetable, dst, src, max);
}

int
proc_exec(const char *requested_name)
{
  char name[32];
  uint64 n = 0, va, image_end, stack_base, stack_top;
  const struct embedded_program *image;
  pagetable_t pt;
  while (requested_name[n] && requested_name[n] != '\n' &&
         requested_name[n] != '\r' && n + 1 < sizeof(name)) {
    name[n] = requested_name[n]; n++;
  }
  name[n] = 0;
  for (image = _uprog_table; image->start; image++)
    if (string_equal(name, image->name)) break;
  if (!image->start || image->end < image->start ||
      image->memory_end < image->end - image->start ||
      image->memory_end >= TRAPFRAME - (LAB3_GUARD_PAGES + USERSTACK) * PGSIZE ||
      image->entry >= image->readonly_end) return -1;
  image_end = PGROUNDUP(image->memory_end);
  stack_base = image_end + LAB3_GUARD_PAGES * PGSIZE;
  stack_top = stack_base + USERSTACK * PGSIZE;
  pt = new_space(current->trapframe);
  if (!pt) return -1;
  for (va = 0; va < image_end; va += PGSIZE) {
    void *page = kalloc();
    int perm = PTE_U | PTE_R | (va < image->readonly_end ? PTE_X : PTE_W);
    if (!page) goto fail;
    if (mappages(pt, va, PGSIZE, (uint64)page, perm) < 0) {
      kfree(page); goto fail;
    }
    if (va < image->end - image->start) {
      uint64 bytes = image->end - image->start - va;
      if (bytes > PGSIZE) bytes = PGSIZE;
      memory_copy(page, (void *)(image->start + va), bytes);
    }
  }
  if (uvm_grow(pt, stack_base, stack_top) < 0) goto fail;
  /* Commit only after all allocations succeed. The trapframe page is stable. */
  pagetable_t old = current->pagetable;
  current->pagetable = pt;
  current->user_satp = MAKE_SATP(pt);
  current->guard_start = image_end;
  current->stack_base = stack_base;
  current->heap_base = current->sz = stack_top;
  memory_zero(current->trapframe, sizeof(*current->trapframe));
  current->trapframe->epc = image->entry;
  current->trapframe->sp = stack_top - 16;
  vm_free(old);
  return 0;
fail:
  vm_free(pt);
  return -1;
}

uint64
proc_sbrk(int n)
{
  uint64 old = current->sz, end;
  if (n >= 0) {
    if ((uint64)n >= TRAPFRAME - old) return (uint64)-1;
    end = old + (uint64)n;
    if (uvm_grow(current->pagetable, old, end) < 0) return (uint64)-1;
  } else {
    uint64 decrease = 0UL - (uint64)(long)n;
    if (decrease > old - current->heap_base) return (uint64)-1;
    end = old - decrease;
    uvmunmap(current->pagetable, PGROUNDUP(end),
             (PGROUNDUP(old) - PGROUNDUP(end)) / PGSIZE, 1);
  }
  current->sz = end;
  return old; /* ABI required by official memcap: base of the allocated interval. */
}

void
proc_init(void)
{
  current = allocate_slot();
  if (!current || proc_exec("sh") < 0) {
    printf("lab3: cannot load embedded sh\n");
    for (;;) asm volatile("wfi");
  }
  current->state = PROC_RUNNING;
  dump_pagetable(current->pagetable);
  printf("lab3 ready: order=%d guard=%d reserve=%d free=%lu\n",
         LAB3_ALLOC_ORDER, LAB3_GUARD_PAGES, LAB3_MEMCAP_DELTA, kfree_count());
}

int
proc_fork(void)
{
  struct proc *child = allocate_slot();
  uint64 va;
  if (!child) return -1;
  for (va = 0; va < current->sz; va += PGSIZE) {
    pte_t *source = walk(current->pagetable, va, 0);
    void *page;
    if (!source || !(*source & PTE_V)) continue; /* Unmapped guard holes. */
    page = kalloc();
    if (!page) goto fail;
    memory_copy(page, (void *)PTE2PA(*source), PGSIZE);
    if (mappages(child->pagetable, va, PGSIZE, (uint64)page,
                 PTE_FLAGS(*source) & (PTE_R | PTE_W | PTE_X | PTE_U)) < 0) {
      kfree(page); goto fail;
    }
  }
  memory_copy(child->trapframe, current->trapframe, sizeof(*child->trapframe));
  child->trapframe->a0 = 0;
  child->sz = current->sz;
  child->heap_base = current->heap_base;
  child->guard_start = current->guard_start;
  child->stack_base = current->stack_base;
  child->parent_pid = current->pid;
  child->state = PROC_RUNNABLE;
  return child->pid;
fail:
  release_slot(child);
  return -1;
}

/* Switching is confined to the outer user-trap boundary: no C continuation. */
static void
select_next(void)
{
  int start = current ? current - processes : 0, i;
  for (;;) {
    for (i = 1; i <= LAB2_NPROC; i++) {
      struct proc *p = &processes[(start + i) % LAB2_NPROC];
      if (p->state == PROC_RUNNABLE) {
        current = p;
        p->state = PROC_RUNNING;
        return;
      }
    }
    /* No kernel continuation is suspended: interrupts only wake candidates. */
    intr_on();
    asm volatile("wfi");
    intr_off();
  }
}

void proc_defer(enum procstate state)
{
  current->trapframe->epc -= 4; /* Retry the same ecall with its original args. */
  current->state = state;
  select_next();
}
void proc_wake_input(void)
{
  int i;
  for (i = 0; i < LAB2_NPROC; i++)
    if (processes[i].state == PROC_INPUT) processes[i].state = PROC_RUNNABLE;
}
void proc_wake_ticks(uint64 now)
{
  int i;
  for (i = 0; i < LAB2_NPROC; i++)
    if (processes[i].state == PROC_SLEEP && now >= processes[i].sleep_until)
      processes[i].state = PROC_RUNNABLE;
}

void
proc_yield(void)
{
  current->state = PROC_RUNNABLE;
  select_next();
}

static int
reap(struct proc *parent, struct proc *child, uint64 status_address)
{
  int pid = child->pid;
  if (status_address && copyout(parent->pagetable, status_address,
                               &child->exit_status, sizeof(int)) < 0) return -1;
  release_slot(child);
  return pid;
}

int
proc_wait(uint64 status_address)
{
  int i, have_child = 0;
  if (status_address &&
      (!within_user(current, status_address, sizeof(int)) ||
       vm_check(current->pagetable, status_address, sizeof(int), PTE_W) < 0)) return -1;
  for (i = 0; i < LAB2_NPROC; i++) {
    struct proc *p = &processes[i];
    if (p->state == PROC_UNUSED || p->parent_pid != current->pid) continue;
    have_child = 1;
    if (p->state == PROC_ZOMBIE) return reap(current, p, status_address);
  }
  if (!have_child) return -1;
  current->wait_status_address = status_address;
  current->state = PROC_WAITING;
  select_next();
  return 0;
}

void
proc_exit(int status)
{
  struct proc *departing = current, *parent = 0;
  int i;
  departing->exit_status = status;
  departing->state = PROC_ZOMBIE;
  for (i = 0; i < LAB2_NPROC; i++) {
    struct proc *p = &processes[i];
    if (p->state != PROC_UNUSED && p->pid == departing->parent_pid) parent = p;
    if (p->state != PROC_UNUSED && p->parent_pid == departing->pid)
      p->parent_pid = processes[0].pid;
  }
  if (parent && parent->state == PROC_WAITING) {
    int result = reap(parent, departing, parent->wait_status_address);
    parent->trapframe->a0 = (uint64)(long)result;
    parent->state = PROC_RUNNABLE;
  }
  select_next();
}

void proc_enter_user(void) { usertrapret(); }
