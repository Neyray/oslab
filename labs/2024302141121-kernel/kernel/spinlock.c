#include "types.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void panic(const char *reason) {
  intr_off(); printf("panic: %s\n", reason);
  for (;;) asm volatile("wfi");
}
void initlock(struct spinlock *lk, const char *name) {
  lk->locked=0; lk->name=name; lk->owner=0;
}
void push_off(void) {
  int enabled=intr_get(); intr_off();
  struct cpu *c=mycpu();
  if(c->noff==0) c->intena=enabled;
  c->noff++;
}
void pop_off(void) {
  struct cpu *c=mycpu();
  if(intr_get() || c->noff<1) panic("pop_off");
  if(--c->noff==0 && c->intena) intr_on();
}
int holding(struct spinlock *lk) {
  return lk->locked && lk->owner==mycpu();
}
void acquire(struct spinlock *lk) {
  push_off(); if(holding(lk)) panic("acquire reentry");
  while(__sync_lock_test_and_set(&lk->locked,1)) ;
  __sync_synchronize(); lk->owner=mycpu();
}
void release(struct spinlock *lk) {
  if(!holding(lk)) panic("release owner");
  lk->owner=0; __sync_synchronize();
  __sync_lock_release(&lk->locked); pop_off();
}
