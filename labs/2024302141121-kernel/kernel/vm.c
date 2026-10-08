#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"
#include "mm.h"

#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)
#define VM_NEW (1UL << 8) /* Temporary RSW bit, never visible after mappages. */
#define LEAF_BITS (PTE_R | PTE_W | PTE_X)
static pagetable_t kernel_table;
extern char trampoline[];

static int
empty_table(pagetable_t pt)
{
  int i;
  for (i = 0; i < 512; i++) if (pt[i] & PTE_V) return 0;
  return 1;
}

/* Roll back only the intermediate pages created by this walk. */
static pte_t *
walk_internal(pagetable_t pt, uint64 va, int alloc, int mark)
{
  pte_t *created[2];
  int level, ncreated = 0;
  if (!pt || va >= MAXVA) return 0;
  for (level = 2; level > 0; level--) {
    pte_t *pte = &pt[PX(level, va)];
    if (*pte & PTE_V) {
      if (*pte & LEAF_BITS) goto fail; /* This implementation uses 4KiB leaves. */
    } else {
      pagetable_t next;
      if (!alloc || !(next = kalloc())) goto fail;
      *pte = PA2PTE(next) | PTE_V | (mark ? VM_NEW : 0);
      created[ncreated++] = pte;
    }
    pt = (pagetable_t)PTE2PA(*pte);
  }
  return &pt[PX(0, va)];
fail:
  while (ncreated) {
    pte_t *pte = created[--ncreated];
    void *page = (void *)PTE2PA(*pte);
    *pte = 0;
    sfence_vma();
    kfree(page);
  }
  return 0;
}

pte_t *walk(pagetable_t pt, uint64 va, int alloc)
{
  return walk_internal(pt, va, alloc, 0);
}

/* Commit clears markers; abort frees only new empty intermediate tables. */
static void
finish_mapping(pagetable_t pt, int level, int abort)
{
  int i;
  if (!level) return;
  for (i = 0; i < 512; i++) {
    pte_t entry = pt[i];
    if (!(entry & PTE_V) || (entry & LEAF_BITS)) continue;
    pagetable_t child = (pagetable_t)PTE2PA(entry);
    finish_mapping(child, level - 1, abort);
    if (abort && (entry & VM_NEW) && empty_table(child)) {
      pt[i] = 0;
      sfence_vma();
      kfree(child);
    } else {
      pt[i] &= ~VM_NEW;
    }
  }
}

static int
map_pages(pagetable_t pt, uint64 va, uint64 size, uint64 pa, int perm, int own_transaction)
{
  uint64 offset;
  if (!pt || !size || (va | pa | size) & (PGSIZE - 1) ||
      va >= MAXVA || size > MAXVA - va || pa + size < pa ||
      !(perm & LEAF_BITS) || ((perm & PTE_W) && !(perm & PTE_R))) return -1;
  /* Detect remaps before changing anything; existing leaves are never owned. */
  for (offset = 0; offset < size; offset += PGSIZE) {
    pte_t *pte = walk(pt, va + offset, 0);
    if (pte && (*pte & PTE_V)) return -1;
  }
  for (offset = 0; offset < size; offset += PGSIZE) {
    pte_t *pte = walk_internal(pt, va + offset, 1, 1);
    if (!pte) {
      uint64 done;
      for (done = 0; done < offset; done += PGSIZE)
        *walk(pt, va + done, 0) = 0;
      sfence_vma();
      if (own_transaction) finish_mapping(pt, 2, 1);
      return -1; /* The caller still owns every supplied physical data page. */
    }
    *pte = PA2PTE(pa + offset) | perm | PTE_V | PTE_A |
           ((perm & PTE_W) ? PTE_D : 0);
  }
  if (own_transaction) finish_mapping(pt, 2, 0);
  sfence_vma();
  return 0;
}

int mappages(pagetable_t pt, uint64 va, uint64 size, uint64 pa, int perm)
{ return map_pages(pt, va, size, pa, perm, 1); }

static void
prune(pagetable_t pt, int level)
{
  int i;
  if (!level) return;
  for (i = 0; i < 512; i++) {
    pte_t entry = pt[i];
    if (!(entry & PTE_V) || (entry & LEAF_BITS)) continue;
    pagetable_t child = (pagetable_t)PTE2PA(entry);
    prune(child, level - 1);
    if (empty_table(child)) {
      pt[i] = 0;
      sfence_vma();
      kfree(child);
    }
  }
}

void
uvmunmap(pagetable_t pt, uint64 va, uint64 pages, int release)
{
  uint64 i;
  if (!pt || (va & (PGSIZE - 1)) || va >= MAXVA || pages > (MAXVA - va) / PGSIZE) return;
  for (i = 0; i < pages; i++, va += PGSIZE) {
    pte_t *pte = walk(pt, va, 0);
    uint64 pa;
    if (!pte || !(*pte & PTE_V) || !(*pte & LEAF_BITS)) continue;
    pa = PTE2PA(*pte);
    *pte = 0; /* Invalidate and flush BEFORE recycling the physical page. */
    sfence_vma();
    if (release) kfree((void *)pa);
  }
  prune(pt, 2);
}

static void
free_level(pagetable_t pt, int level, uint64 base)
{
  int i;
  for (i = 0; i < 512; i++) {
    pte_t entry = pt[i];
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    if (!(entry & PTE_V)) continue;
    pt[i] = 0;
    sfence_vma();
    if (!(entry & LEAF_BITS) && level)
      free_level((pagetable_t)PTE2PA(entry), level - 1, va);
    else if (va != TRAMPOLINE && va != TRAPFRAME)
      kfree((void *)PTE2PA(entry));
  }
  kfree(pt);
}
void vm_free(pagetable_t pt) { if (pt) free_level(pt, 2, 0); }

static uint64
user_page(pagetable_t pt, uint64 va, int permission)
{
  pte_t *pte = walk(pt, va, 0);
  if (!pte || (*pte & (PTE_V | PTE_U | permission)) !=
              (PTE_V | PTE_U | permission)) return 0;
  return PTE2PA(*pte);
}

int
vm_check(pagetable_t pt, uint64 va, uint64 n, int permission)
{
  uint64 last, page;
  if (va >= MAXVA || n > MAXVA - va) return -1;
  if (!n) return 0;
  last = PGROUNDDOWN(va + n - 1);
  for (page = PGROUNDDOWN(va); ; page += PGSIZE) {
    if (!user_page(pt, page, permission)) return -1;
    if (page == last) break;
  }
  return 0;
}

static int
copy_user(pagetable_t pt, uint64 va, void *buffer, uint64 n, int to_user)
{
  uint8 *bytes = buffer;
  if (vm_check(pt, va, n, to_user ? PTE_W : PTE_R) < 0) return -1;
  while (n) {
    uint64 offset = va & (PGSIZE - 1), chunk = PGSIZE - offset;
    uint64 pa = user_page(pt, va, to_user ? PTE_W : PTE_R);
    if (chunk > n) chunk = n;
    if (to_user) memory_copy((void *)(pa + offset), bytes, chunk);
    else memory_copy(bytes, (void *)(pa + offset), chunk);
    bytes += chunk; va += chunk; n -= chunk;
  }
  return 0;
}
int copyin(pagetable_t pt, void *dst, uint64 src, uint64 n)
{ return copy_user(pt, src, dst, n, 0); }
int copyout(pagetable_t pt, uint64 dst, const void *src, uint64 n)
{ return copy_user(pt, dst, (void *)src, n, 1); }

int
copyinstr(pagetable_t pt, char *dst, uint64 src, uint64 max)
{
  uint64 used = 0;
  if (src >= MAXVA || max > MAXVA - src) return -1;
  while (used < max) {
    uint64 va = src + used, pa = user_page(pt, va, PTE_R);
    uint64 n = PGSIZE - (va & (PGSIZE - 1)), i;
    if (!pa) return -1;
    if (n > max - used) n = max - used;
    const char *in = (const char *)(pa + (va & (PGSIZE - 1)));
    for (i = 0; i < n; i++) {
      dst[used++] = in[i];
      if (!in[i]) return 0;
    }
  }
  return -1;
}

int
uvm_grow(pagetable_t pt, uint64 old, uint64 end)
{
  uint64 va, begin = PGROUNDUP(old);
  if (end >= TRAPFRAME || end < old) return -1;
  for (va = begin; va < end; va += PGSIZE) {
    void *page = kalloc();
    if (!page || map_pages(pt, va, PGSIZE, (uint64)page,
                           PTE_R | PTE_W | PTE_U, 0) < 0) {
      uint64 undo;
      if (page) kfree(page);
      for (undo = begin; undo < va; undo += PGSIZE) {
        pte_t *pte = walk(pt, undo, 0);
        void *physical = (void *)PTE2PA(*pte);
        *pte = 0;
        sfence_vma();
        kfree(physical);
      }
      finish_mapping(pt, 2, 1);
      return -1;
    }
  }
  finish_mapping(pt, 2, 0);
  sfence_vma();
  return 0;
}

static void
dump_level(pagetable_t pt, int level, uint64 base)
{
  int i;
  for (i = 0; i < 512; i++) {
    pte_t entry = pt[i];
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    if (!(entry & PTE_V)) continue;
    if (entry & LEAF_BITS) {
      printf("LEAF va=%p pa=%p perm=%c%c%c%c\n", (void *)va,
             (void *)PTE2PA(entry), entry & PTE_R ? 'r' : '-',
             entry & PTE_W ? 'w' : '-', entry & PTE_X ? 'x' : '-',
             entry & PTE_U ? 'u' : '-');
    } else {
      printf("L%d va=%p pa=%p perm=----\n", level, (void *)va,
             (void *)PTE2PA(entry));
      if (level) dump_level((pagetable_t)PTE2PA(entry), level - 1, va);
    }
  }
}
void dump_pagetable(pagetable_t pt)
{
  printf("DUMP-PAGETABLE begin\n");
  dump_level(pt, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}

void
vm_init(void)
{
  kinit();
#ifdef LAB3_MM_SELFTEST
  mm_selftest();
#endif
  kernel_table = kalloc();
  if (!kernel_table ||
      mappages(kernel_table, KERNBASE, PHYSTOP - KERNBASE, KERNBASE,
               PTE_R | PTE_W | PTE_X) < 0 ||
      mappages(kernel_table, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0 ||
      mappages(kernel_table, PLIC, 0x400000, PLIC, PTE_R | PTE_W) < 0 ||
      mappages(kernel_table, CLINT_BASE, 0x10000, CLINT_BASE, PTE_R | PTE_W) < 0 ||
      mappages(kernel_table, TRAMPOLINE, PGSIZE,
               PGROUNDDOWN((uint64)trampoline), PTE_R | PTE_X) < 0) {
    printf("lab3: cannot construct kernel page table\n");
    for (;;) asm volatile("wfi");
  }
  sfence_vma();
  w_satp(MAKE_SATP(kernel_table));
  sfence_vma();
}
uint64 kernel_satp_value(void) { return MAKE_SATP(kernel_table); }
