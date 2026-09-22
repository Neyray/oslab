#ifndef OSLAB_PROC_H
#define OSLAB_PROC_H

#include "types.h"

#define LAB2_NPROC 8
#define LAB2_USER_SIZE (64 * 1024UL)
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

enum procstate { PROC_UNUSED, PROC_RUNNABLE, PROC_RUNNING, PROC_WAITING };

struct proc {
  int pid;
  int parent_pid;
  enum procstate state;
  int exit_status;
  struct trapframe *trapframe;
  uint8 *memory;
  uint64 *pagetable;
  uint64 user_satp;
  uint64 kernel_stack_top;
};

void vm_init(void);
uint64 kernel_satp_value(void);
void proc_init(void);
struct proc *myproc(void);
int proc_fork(void);
int proc_exec(const char *name);
int proc_wait(void);
void proc_exit(int status);
int proc_copyin(void *dst, uint64 src, uint64 length);
int proc_copyout(uint64 dst, const void *src, uint64 length);
int proc_copyinstr(char *dst, uint64 src, uint64 max);
void proc_enter_user(void) __attribute__((noreturn));

#endif
