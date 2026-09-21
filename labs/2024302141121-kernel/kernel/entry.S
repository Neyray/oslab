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

        /*
         * The aligned stack storage lives in start.c. RISC-V stacks grow
         * downward, so add its configured byte size to obtain the initial sp.
         */
        la sp, boot_stack
        li t0, LAB1_STACK_KB * 1024
        add sp, sp, t0
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
