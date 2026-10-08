#ifndef OSLAB_MM_H
#define OSLAB_MM_H
#include "types.h"
#include "riscv.h"
#define VM_READ PTE_R
#define VM_WRITE PTE_W
void memory_zero(void *, uint64);
void memory_copy(void *, const void *, uint64);
void kinit(void);
void *kalloc(void);
void kfree(void *);
uint64 kfree_count(void);
uint64 kpage_start(void);
uint64 kpage_limit(void);
pte_t *walk(pagetable_t, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, int);
void uvmunmap(pagetable_t, uint64, uint64, int);
void vm_free(pagetable_t);
int vm_check(pagetable_t, uint64, uint64, int);
int copyin(pagetable_t, void *, uint64, uint64);
int copyout(pagetable_t, uint64, const void *, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);
int uvm_grow(pagetable_t, uint64, uint64);
void dump_pagetable(pagetable_t);
void vm_init(void);
uint64 kernel_satp_value(void);
void mm_selftest(void);
#endif
