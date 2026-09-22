#ifndef OSLAB_DEFS_H
#define OSLAB_DEFS_H

#include "types.h"

int printf(const char *format, ...);
void console_putc(int c);
void console_enable_interrupts(void);
void console_intr(void);
int console_read(void *destination, int length);
void trap_init(void);
void usertrap(void) __attribute__((noreturn));
void usertrapret(void) __attribute__((noreturn));
void kerneltrap(void);
void syscall(void);
uint64 ticks_read(void);

#endif
