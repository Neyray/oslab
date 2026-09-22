#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "proc.h"
#include "defs.h"

#define SCAUSE_INTERRUPT (1UL << 63)
#define SCAUSE_CODE_MASK (~SCAUSE_INTERRUPT)
#define IRQ_S_SOFTWARE 1
#define IRQ_S_EXTERNAL 9
#define SIE_SSIE (1UL << 1)

extern char trampoline[];
extern char uservec[];
extern char userret[];
extern char kernelvec[];

static volatile uint64 ticks;

static uint64
trampoline_address(char *symbol)
{
  uint64 physical_page = PGROUNDDOWN((uint64)trampoline);
  return TRAMPOLINE + ((uint64)symbol - physical_page);
}

static void
plic_init(void)
{
  *(volatile uint32 *)(PLIC + UART0_IRQ * 4) = 1;
  *(volatile uint32 *)PLIC_SENABLE(0) |= (1U << UART0_IRQ);
  *(volatile uint32 *)PLIC_SPRIORITY(0) = 0;
  io_fence();
}

static int
device_interrupt(uint64 cause)
{
  uint64 code = cause & SCAUSE_CODE_MASK;

  if ((cause & SCAUSE_INTERRUPT) == 0)
    return 0;
  if (code == IRQ_S_SOFTWARE) {
    w_sip(r_sip() & ~2UL);
    ticks++;
    return 1;
  }
  if (code == IRQ_S_EXTERNAL) {
    uint32 irq = *(volatile uint32 *)PLIC_SCLAIM(0);
    if (irq == UART0_IRQ)
      console_intr();
    if (irq != 0)
      *(volatile uint32 *)PLIC_SCLAIM(0) = irq;
    io_fence();
    return 1;
  }
  return 0;
}

void
trap_init(void)
{
  intr_off();
  w_stvec((uint64)kernelvec);
  plic_init();
  console_enable_interrupts();
  ticks = 0;
  w_sie(r_sie() | SIE_SEIE | SIE_SSIE);
}

uint64
ticks_read(void)
{
  return ticks;
}

void
usertrap(void)
{
  struct proc *process = myproc();
  uint64 cause = r_scause();

  w_stvec((uint64)kernelvec);
  process->trapframe->epc = r_sepc();

  if (cause == 8) {
    process->trapframe->epc += 4;
    syscall();
  } else if (!device_interrupt(cause)) {
    printf("user trap: scause=%p sepc=%p stval=%p\n", (void *)cause,
           (void *)r_sepc(), (void *)r_stval());
    for (;;)
      asm volatile("wfi");
  }

  usertrapret();
}

void
kerneltrap(void)
{
  uint64 saved_sepc = r_sepc();
  uint64 saved_sstatus = r_sstatus();
  uint64 cause = r_scause();

  if ((saved_sstatus & SSTATUS_SPP) == 0 || !device_interrupt(cause)) {
    printf("kernel trap: scause=%p sepc=%p stval=%p\n", (void *)cause,
           (void *)saved_sepc, (void *)r_stval());
    for (;;)
      asm volatile("wfi");
  }

  w_sepc(saved_sepc);
  w_sstatus(saved_sstatus);
}

void
usertrapret(void)
{
  struct proc *process = myproc();
  uint64 status;
  uint64 return_address;
  void (*return_to_user)(uint64);

  intr_off();
  w_stvec(trampoline_address(uservec));

  process->trapframe->kernel_satp = kernel_satp_value();
  process->trapframe->kernel_sp = process->kernel_stack_top;
  process->trapframe->kernel_trap = (uint64)usertrap;
  process->trapframe->kernel_hartid = r_tp();

  status = r_sstatus();
  status &= ~SSTATUS_SPP;
  status |= SSTATUS_SPIE;
  w_sstatus(status);
  w_sepc(process->trapframe->epc);

  return_address = trampoline_address(userret);
  return_to_user = (void (*)(uint64))return_address;
  return_to_user(process->user_satp);

  for (;;)
    asm volatile("wfi");
}
