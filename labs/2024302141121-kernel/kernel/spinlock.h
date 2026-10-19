#ifndef OSLAB_SPINLOCK_H
#define OSLAB_SPINLOCK_H
struct cpu;
struct spinlock { unsigned int locked; const char *name; struct cpu *owner; };
void initlock(struct spinlock *, const char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int holding(struct spinlock *);
void push_off(void);
void pop_off(void);
void panic(const char *) __attribute__((noreturn));
#endif
