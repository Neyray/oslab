#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"

#define MSTATUS_MIE (1L << 3)
#define MIE_MTIE (1L << 7)
#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000 + 8 * (hart))
#define CLINT_MTIME (CLINT_BASE + 0xbff8)
#define TIMER_BASE_INTERVAL 100000UL

extern void main(void);
extern void timervec(void);

static uint64 timer_scratch[5];

/*
 * Lab1 runs C code only on hart 0, so one boot stack is sufficient. Keep its
 * capacity tied directly to the per-student configuration used by entry.S.
 */
uint8 boot_stack[LAB1_STACK_KB * 1024] __attribute__((aligned(16)));

/*
 * Finish the machine-mode obligations and enter supervisor mode. Interrupts
 * stay disabled because lab1 intentionally has no S-mode trap handler.
 */
void __attribute__((noreturn))
start(void)
{
  uint64 mstatus = r_mstatus();

  mstatus &= ~MSTATUS_MPP_MASK;
  mstatus |= MSTATUS_MPP_S;
  mstatus &= ~MSTATUS_MIE;
  w_mstatus(mstatus);
  w_mepc((uint64)main);

  /* Delegate what can be delegated, but do not enable any S-mode source yet. */
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(0);

  /*
   * This QEMU CPU does not expose Sstc. Bridge the machine timer into a
   * delegated supervisor software interrupt instead.
   */
  timer_scratch[3] = CLINT_MTIMECMP(0);
  timer_scratch[4] = TIMER_BASE_INTERVAL * LAB2_TICK;
  *(volatile uint64 *)CLINT_MTIMECMP(0) =
    *(volatile uint64 *)CLINT_MTIME + timer_scratch[4];
  asm volatile("csrw mscratch, %0" : : "r"(timer_scratch));
  asm volatile("csrw mtvec, %0" : : "r"(timervec));
  w_mie(r_mie() | MIE_MTIE);

  /*
   * NAPOT over the physical address space, R/W/X allowed. Without this PMP
   * entry, modern QEMU faults on the first S-mode instruction after mret.
   */
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  /* main() installs the minimal lab2 user/trampoline mappings later. */
  w_satp(0);
  sfence_vma();

  w_tp(r_mhartid());
  asm volatile("mret");

  for (;;)
    asm volatile("wfi");
}
