#ifndef OSLAB_PROC_H
#define OSLAB_PROC_H

#include "types.h"
#include "param.h"
#include "spinlock.h"

#define LAB2_NPROC NPROC
#define LAB2_KSTACK_SIZE (8 * 1024UL)

struct trapframe {
  uint64 kernel_satp;
  uint64 kernel_sp;
  uint64 kernel_trap;
  uint64 epc;
  uint64 kernel_hartid;
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
};

/* Must match the read-only swtch.S byte offsets exactly. */
struct context {
  uint64 ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};
struct cpu {
  struct context scheduler;
  struct proc *proc;
  int noff, intena;
};
enum procstate { PROC_UNUSED, PROC_USED, PROC_RUNNABLE, PROC_RUNNING, PROC_SLEEPING, PROC_ZOMBIE };

struct proc {
  struct spinlock lock;
  struct context context;
  void *chan;
  int killed;
  int cpu_ticks, last_slice_ticks, tickets, mlfq_level;
  char name[32];
  int pid;
  int parent_pid;
  enum procstate state;
  int exit_status;
  struct trapframe *trapframe;
  uint64 *pagetable;
  uint64 user_satp;
  uint64 kernel_stack_top;
  uint64 sz;
  uint64 heap_base;
  uint64 guard_start;
  uint64 stack_base;
};

void vm_init(void);
uint64 kernel_satp_value(void);
void proc_init(void);
struct proc *myproc(void);
int proc_fork(void);
int proc_exec(const char *name);
int proc_wait(uint64);
uint64 proc_sbrk(int);
void proc_yield(void);
struct cpu *mycpu(void);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void swtch(struct context *, struct context *);
void proc_sleep(void *, struct spinlock *);
void proc_wakeup(void *);
void proc_tick(void);
int proc_kill(int);
int proc_killed(void);
int proc_schedstat(int, uint64, uint64);
int proc_setickets(int);
int ticks_pause(int);
int proc_validate(uint64, uint64, int);
void proc_dump_current(void);
void proc_exit(int status) __attribute__((noreturn));
int proc_copyin(void *dst, uint64 src, uint64 length);
int proc_copyout(uint64 dst, const void *src, uint64 length);
int proc_copyinstr(char *dst, uint64 src, uint64 max);
void proc_enter_user(void) __attribute__((noreturn));

#endif
