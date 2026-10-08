/* In-kernel failure injection by consuming pages; production build is empty. */
#ifdef LAB3_MM_SELFTEST
#include "types.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"
#include "mm.h"
static uint64 expected[32768];
static int failures;
static int all_zero = 1;
static void check(const char *name, int ok)
{
  printf("MMTEST %s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}
static void *
hold_until(uint64 left)
{
  void *head = 0;
  while (kfree_count() > left) {
    void *page = kalloc();
    uint64 i;
    for (i = 0; i < PGSIZE; i++) if (((uint8 *)page)[i]) all_zero = 0;
    *(void **)page = head;
    head = page;
  }
  return head;
}
static void release_held(void *head)
{
  while (head) {
    void *next = *(void **)head;
    kfree(head);
    head = next;
  }
}
void
mm_selftest(void)
{
  uint64 initial = kfree_count(), i, j, remaining = initial;
  void *held = 0;
  int zero = 1, order = 1;
  for (i = 0; i < initial; i++) expected[i] = kpage_start() + i * PGSIZE;
  for (i = 0; i < 8; i++) {
#if LAB3_ALLOC_ORDER == 0
    uint64 rank = 0;
#elif LAB3_ALLOC_ORDER == 1
    uint64 rank = remaining - 1;
#else
    uint64 rank = (remaining - 1) / 2;
#endif
    void *page = kalloc();
    if ((uint64)page != expected[rank]) order = 0;
    for (j = 0; j < PGSIZE; j++) if (((uint8 *)page)[j]) zero = 0;
    for (j = rank; j + 1 < remaining; j++) expected[j] = expected[j + 1];
    remaining--;
    *(void **)page = held; held = page;
  }
  check("allocation order/alignment/zero", order && zero);
  release_held(held);
  check("allocator conservation/reserve", kfree_count() == initial &&
        kpage_limit() - kpage_start() == initial * PGSIZE);
  void *page = kalloc();
  ((uint8 *)page)[100] = 99;
  kfree(page);
  /* All pages are freshly zeroed even after poison/reallocation. */
  held = hold_until(0);
  check("exhaustion returns NULL and recycled pages zeroed", kalloc() == 0 && kfree_count() == 0 && all_zero);
  release_held(held);

  pagetable_t pt = kalloc();
  void *data = kalloc();
  ((uint8 *)data)[0] = 77;
  check("initial mapping", mappages(pt, 0, PGSIZE, (uint64)data, PTE_R | PTE_W | PTE_U) == 0);
  pte_t *old_empty = walk(pt, 1UL << 30, 1);
  uint64 before = kfree_count();
  check("remap rejection", mappages(pt, 0, PGSIZE, (uint64)data, PTE_R) == -1 && kfree_count() == before);
  held = hold_until(2);
  int result = mappages(pt, (2UL << 30) - PGSIZE, 3 * PGSIZE, (uint64)data, PTE_R | PTE_U);
  pte_t *left = walk(pt, (2UL << 30) - PGSIZE, 0);
  check("mapping rollback preserves old empty tables", result == -1 &&
        kfree_count() == 2 && (!left || !(*left & PTE_V)) &&
        walk(pt, 1UL << 30, 0) == old_empty && ((uint8 *)data)[0] == 77);
  page = kalloc();
  check("walk rollback", !walk(pt, 3UL << 30, 1) && kfree_count() == 1);
  kfree(page);
  check("growth rollback", uvm_grow(pt, 0x400000, 0x404000) == -1 && kfree_count() == 2);
  release_held(held);
  vm_free(pt);
  check("complete teardown restores all pages", kfree_count() == initial);

  pt = kalloc();
  void *a = kalloc(), *b = kalloc();
  mappages(pt, 0, PGSIZE, (uint64)a, PTE_R | PTE_U);
  mappages(pt, PGSIZE, PGSIZE, (uint64)b, PTE_R | PTE_W | PTE_U);
  char got[6];
  memory_copy((uint8 *)a + PGSIZE - 3, "abc", 3);
  memory_copy(b, "de", 3);
  check("copyinstr/copyin cross physical pages", copyinstr(pt, got, PGSIZE - 3, 6) == 0 &&
        got[0] == 'a' && got[4] == 'e' && got[5] == 0 &&
        copyin(pt, got, PGSIZE - 3, 6) == 0);
  check("copyout preflight prevents partial writes", copyout(pt, PGSIZE - 1, "XY", 2) == -1 &&
        ((char *)a)[PGSIZE - 1] == 'c' && ((char *)b)[0] == 'd');
  check("invalid/unmapped/overflow rejected", vm_check(pt, 2 * PGSIZE, 1, PTE_R) == -1 &&
        copyin(pt, got, MAXVA - 1, 6) == -1 && copyinstr(pt, got, 0, 0) == -1);
  vm_free(pt);
  check("final free-page conservation", kfree_count() == initial);
  printf("MMTEST RESULT failures=%d\n", failures);
}
#endif
