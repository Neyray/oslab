#include "course_sid.h"

        .section .text
        .globl _entry
        .globl machine_trap_vector

/*
 * QEMU's reset ROM transfers control here in machine mode. Nothing below
 * may use the C environment until a stack and a trap-vector base exist.
 */
_entry:
        /* Disable global M-mode interrupts, then disable every M source. */
        csrci mstatus, 8
        csrw mie, zero

        /* mtvec direct mode: the base must be four-byte aligned. */
        la t0, machine_trap_vector
        csrw mtvec, t0

        /* lab1 is deliberately single-core. Only hart 0 may touch UART. */
        csrr a0, mhartid
        bnez a0, .Lpark

        /* RISC-V stacks grow downward; start at the first byte past it. */
        la sp, boot_stack_top
        call start

/* start() never returns. Park safely if that contract is ever broken. */
.Lpark:
        wfi
        j .Lpark

        .balign 4
machine_trap_vector:
        /* No recovery is possible before lab2 installs real trap handling. */
        csrw mie, zero
.Ltrap_park:
        wfi
        j .Ltrap_park

        .section .bss.stack,"aw",@nobits
        .balign 16
        .globl boot_stack
        .globl boot_stack_top
boot_stack:
        .space LAB1_STACK_KB * 1024
boot_stack_top:
