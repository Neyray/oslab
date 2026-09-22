#include "types.h"
#include "riscv.h"
#include "syscall.h"
#include "proc.h"
#include "defs.h"

static uint64
argraw(int index)
{
  struct trapframe *frame = myproc()->trapframe;

  switch (index) {
  case 0: return frame->a0;
  case 1: return frame->a1;
  case 2: return frame->a2;
  case 3: return frame->a3;
  case 4: return frame->a4;
  case 5: return frame->a5;
  default: return 0;
  }
}

static int
sys_write(void)
{
  char buffer[128];
  uint64 address = argraw(1);
  int descriptor = (int)argraw(0);
  int requested = (int)argraw(2);
  int written = 0;

  if ((descriptor != 1 && descriptor != 2) || requested < 0)
    return -1;
  while (written < requested) {
    int chunk = requested - written;
    int index;
    if (chunk > (int)sizeof(buffer))
      chunk = sizeof(buffer);
    if (proc_copyin(buffer, address + written, chunk) < 0)
      return -1;
    for (index = 0; index < chunk; index++)
      console_putc(buffer[index]);
    written += chunk;
  }
  return written;
}

static int
sys_read(void)
{
  char buffer[256];
  uint64 address = argraw(1);
  int descriptor = (int)argraw(0);
  int requested = (int)argraw(2);
  int received;

  if (descriptor != 0 || requested < 0)
    return -1;
  if (requested == 0)
    return 0;
  if (requested > (int)sizeof(buffer))
    requested = sizeof(buffer);
  received = console_read(buffer, requested);
  if (received < 0 || proc_copyout(address, buffer, received) < 0)
    return -1;
  return received;
}

static int
sys_exec(void)
{
  char name[32];
  if (proc_copyinstr(name, argraw(0), sizeof(name)) < 0)
    return -1;
  return proc_exec(name);
}

static int
sys_pause(void)
{
  int duration = (int)argraw(0);
  uint64 target;

  if (duration < 0)
    return -1;
  target = ticks_read() + (uint64)duration;
  while (ticks_read() < target) {
    intr_on();
    asm volatile("wfi");
    intr_off();
  }
  return 0;
}

void
syscall(void)
{
  struct proc *caller = myproc();
  int number = (int)caller->trapframe->a7;
  int result;

  switch (number) {
  case SYS_fork:
    result = proc_fork();
    break;
  case SYS_exit:
    proc_exit((int)argraw(0));
    result = 0;
    break;
  case SYS_wait:
    result = proc_wait();
    break;
  case SYS_read:
    result = sys_read();
    break;
  case SYS_exec:
    result = sys_exec();
    break;
  case SYS_getpid:
    result = caller->pid;
    break;
  case SYS_pause:
    result = sys_pause();
    break;
  case SYS_uptime:
    result = (int)ticks_read();
    break;
  case SYS_write:
    result = sys_write();
    break;
  default:
    result = -1;
    break;
  }

  if (myproc() == caller && caller->state == PROC_RUNNING)
    caller->trapframe->a0 = (uint64)result;
}
