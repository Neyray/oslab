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
static struct cpu cpus[NCPU];
static struct spinlock wait_lock;
static struct spinlock pid_lock;
static struct proc *initproc;
#define current (myproc())
#if LAB4_POLICY_ID < 0 || LAB4_POLICY_ID > 2
#error "This kernel implements the assigned MLFQ policy family only"
#endif
#define MLFQ_LEVELS (LAB4_POLICY_ID + 2)
static void forkret(void) __attribute__((noreturn));
typedef char context_size_must_match[sizeof(struct context)==112 ? 1 : -1];
typedef char context_s11_must_match[__builtin_offsetof(struct context,s11)==104 ? 1 : -1];
static int next_pid = 1;
typedef char trapframe_a0_offset_must_match[
  __builtin_offsetof(struct trapframe, a0) == 112 ? 1 : -1];
typedef char trapframe_t6_offset_must_match[
  __builtin_offsetof(struct trapframe, t6) == 280 ? 1 : -1];
typedef char embedded_table_size_must_match[sizeof(struct embedded_program) == 72 ? 1 : -1];

struct cpu *mycpu(void) { return &cpus[r_tp()]; }
struct proc *myproc(void) {
  push_off(); struct proc *p=mycpu()->proc; pop_off(); return p;
}

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

/* Caller owns p->lock; static kernel stacks are reserved for the tree's lifetime. */
static void
release_slot(struct proc *p)
{
  vm_free(p->pagetable);
  if (p->trapframe) kfree(p->trapframe);
  /* Keep the live lock intact, including its owner and locked bit. */
  memory_zero(&p->context, sizeof(*p)-__builtin_offsetof(struct proc,context));
  p->state=PROC_UNUSED;
}

static struct proc *
allocate_slot(void)
{
  for (int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i];
    acquire(&p->lock);
    if(p->state!=PROC_UNUSED) { release(&p->lock); continue; }
    p->state=PROC_USED;
    p->trapframe=kalloc();
    if(!p->trapframe) goto fail;
    p->pagetable=new_space(p->trapframe);
    if(!p->pagetable) goto fail;
    p->kernel_stack_top=(uint64)&kernel_stacks[i][LAB2_KSTACK_SIZE];
    acquire(&pid_lock); p->pid=next_pid++; release(&pid_lock);
    p->user_satp=MAKE_SATP(p->pagetable);
    p->context.ra=(uint64)forkret;
    p->context.sp=p->kernel_stack_top;
    p->tickets=7+COURSE_SID%13;
    return p; /* USED, private, and locked until the creator publishes it. */
fail:
    release_slot(p); release(&p->lock); return 0;
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
  static const char *states[]={"UNUSED","USED","RUNNABLE","RUNNING","SLEEPING","ZOMBIE"};
  struct proc *target=current ? current : initproc;
  if(target && target->pagetable) {
    dump_pagetable(target->pagetable);
    printf("lab3 memory: free=%lu pid=%d brk=%p\n",kfree_count(),target->pid,(void*)target->sz);
  }
  printf("PID PPID STATE LEVEL CPU SLICE TICKETS NAME\n");
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i]; acquire(&p->lock);
    if(p->state!=PROC_UNUSED)
      printf("%d %d %s %d %d %d %d %s\n",p->pid,p->parent_pid,states[p->state],
             p->mlfq_level,p->cpu_ticks,p->last_slice_ticks,p->tickets,p->name);
    release(&p->lock);
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
  char name[32]={0};
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
  acquire(&current->lock);
  memory_copy(current->name,name,sizeof(name));
  current->mlfq_level=0;
  release(&current->lock);
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
  initlock(&wait_lock,"wait"); initlock(&pid_lock,"pid");
  for(int i=0;i<NPROC;i++) initlock(&processes[i].lock,"proc");
  struct proc *p=allocate_slot();
  if(!p) panic("initial process");
  initproc=p;
  mycpu()->proc=p;
  release(&p->lock);
  if(proc_exec("sh")<0) panic("initial sh");
  dump_pagetable(p->pagetable);
  printf("lab3 ready: order=%d guard=%d reserve=%d free=%lu\n",
         LAB3_ALLOC_ORDER,LAB3_GUARD_PAGES,LAB3_MEMCAP_DELTA,kfree_count());
  acquire(&p->lock); p->state=PROC_RUNNABLE; mycpu()->proc=0; release(&p->lock);
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
  memory_copy(child->name,current->name,sizeof(child->name));
  int pid=child->pid;
  release(&child->lock); /* Do not invert wait_lock -> p->lock. Still USED. */
  acquire(&wait_lock); child->parent_pid=current->pid; release(&wait_lock);
  acquire(&child->lock); child->state=PROC_RUNNABLE; release(&child->lock);
  return pid;
fail:
  release_slot(child);
  release(&child->lock);
  return -1;
}

/* The boot stack becomes CPU 0's independent scheduler stack. */
void
scheduler(void)
{
  struct cpu *c=mycpu(); int rr[MLFQ_LEVELS];
  for(int i=0;i<MLFQ_LEVELS;i++) rr[i]=NPROC-1;
  c->proc=0;
  for(;;) {
    intr_on(); intr_off();
    struct proc *chosen=0;
    for(int level=0;level<MLFQ_LEVELS && !chosen;level++) {
      for(int step=1;step<=NPROC;step++) {
        struct proc *p=&processes[(rr[level]+step)%NPROC];
        acquire(&p->lock);
#ifdef LAB4_RR_TEST
        int eligible=p->state==PROC_RUNNABLE;
#else
        int eligible=p->state==PROC_RUNNABLE && p->mlfq_level==level;
#endif
        if(eligible) { chosen=p; break; }
        release(&p->lock);
      }
    }
    if(!chosen) { asm volatile("wfi"); continue; }
    struct proc *p=chosen;
#ifdef LAB4_RR_TEST
    rr[0]=p-processes;
#else
    rr[p->mlfq_level]=p-processes;
#endif
    p->state=PROC_RUNNING; p->last_slice_ticks=0; c->proc=p;
    swtch(&c->scheduler,&p->context);
    c->proc=0;
    release(&p->lock);
  }
}

void
sched(void)
{
  struct proc *p=current; struct cpu *c=mycpu();
  if(!p || !holding(&p->lock)) panic("sched p->lock");
  if(c->noff!=1) panic("sched locks");
  if(p->state==PROC_RUNNING) panic("sched running");
  if(intr_get()) panic("sched interruptible");
  int enabled=c->intena;
  swtch(&p->context,&c->scheduler);
  mycpu()->intena=enabled;
}

static void forkret(void)
{
  release(&current->lock);
  usertrapret();
}

void proc_yield(void)
{
  struct proc *p=current;
  acquire(&p->lock); p->state=PROC_RUNNABLE; sched(); release(&p->lock);
}

void proc_tick(void)
{
  struct proc *p=current; int expired;
  acquire(&p->lock);
  p->cpu_ticks++; p->last_slice_ticks++;
#ifdef LAB4_RR_TEST
  expired=1;
#else
  expired=p->last_slice_ticks >= (1 << p->mlfq_level);
  if(expired && p->mlfq_level<MLFQ_LEVELS-1) p->mlfq_level++;
#endif
  release(&p->lock);
  if(expired) proc_yield();
}

void proc_sleep(void *chan, struct spinlock *lk)
{
  struct proc *p=current;
  if(!p || lk==&p->lock || !holding(lk)) panic("sleep condition lock");
  acquire(&p->lock);
  release(lk);
  p->chan=chan; p->state=PROC_SLEEPING;
  sched();
  p->chan=0;
  release(&p->lock);
  acquire(lk);
}

void proc_wakeup(void *chan)
{
  struct proc *self=current;
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i];
    if(p==self) continue;
    acquire(&p->lock);
    if(p->state==PROC_SLEEPING && p->chan==chan) p->state=PROC_RUNNABLE;
    release(&p->lock);
  }
}

int proc_killed(void)
{
  struct proc *p=current;
  acquire(&p->lock); int killed=p->killed; release(&p->lock); return killed;
}

int proc_kill(int pid)
{
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i]; acquire(&p->lock);
    if(p->pid==pid && p->state!=PROC_UNUSED && p->state!=PROC_USED && p->state!=PROC_ZOMBIE) {
      p->killed=1;
      if(p->state==PROC_SLEEPING) p->state=PROC_RUNNABLE;
      release(&p->lock); return 0;
    }
    release(&p->lock);
  }
  return -1;
}

int proc_wait(uint64 status_address)
{
  struct proc *parent=current;
  if(status_address && proc_validate(status_address,sizeof(int),PTE_W)<0) return -1;
  acquire(&wait_lock);
  for(;;) {
    int have_child=0;
    for(int i=0;i<NPROC;i++) {
      struct proc *p=&processes[i];
      if(p->parent_pid!=parent->pid) continue;
      acquire(&p->lock); have_child=1;
      if(p->state==PROC_ZOMBIE) {
        int pid=p->pid;
        if(status_address && proc_copyout(status_address,&p->exit_status,sizeof(int))<0) {
          release(&p->lock); release(&wait_lock); return -1;
        }
        release_slot(p); release(&p->lock); release(&wait_lock); return pid;
      }
      release(&p->lock);
    }
    if(!have_child || proc_killed()) { release(&wait_lock); return -1; }
    proc_sleep(parent,&wait_lock);
  }
}

void proc_exit(int status)
{
  struct proc *p=current;
  if(p==initproc) panic("initial shell exit");
  acquire(&wait_lock);
  for(int i=0;i<NPROC;i++) {
    struct proc *q=&processes[i];
    if(q->parent_pid==p->pid) {
      q->parent_pid=initproc->pid;
      proc_wakeup(initproc);
    }
  }
  for(int i=0;i<NPROC;i++)
    if(processes[i].pid==p->parent_pid) proc_wakeup(&processes[i]);
  acquire(&p->lock);
  p->exit_status=status; p->state=PROC_ZOMBIE;
  release(&wait_lock);
  sched();
  panic("zombie resumed");
}

int proc_schedstat(int pid, uint64 cpu_address, uint64 slice_address)
{
  if(proc_validate(cpu_address,sizeof(int),PTE_W)<0 ||
     proc_validate(slice_address,sizeof(int),PTE_W)<0) return -1;
  if(pid==-1) pid=current->pid;
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i]; acquire(&p->lock);
    if(p->pid==pid && p->state!=PROC_UNUSED && p->state!=PROC_USED && p->state!=PROC_ZOMBIE) {
      int cpu=p->cpu_ticks, slice=p->last_slice_ticks;
      release(&p->lock);
      return proc_copyout(cpu_address,&cpu,sizeof(cpu))<0 ||
             proc_copyout(slice_address,&slice,sizeof(slice))<0 ? -1 : 0;
    }
    release(&p->lock);
  }
  return -1;
}

int proc_setickets(int n)
{
  if(n<1) return -1;
  struct proc *p=current;
  acquire(&p->lock); p->tickets=n; release(&p->lock); return 0;
}

void proc_enter_user(void) { scheduler(); }
