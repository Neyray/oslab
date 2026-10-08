#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "defs.h"
#include "mm.h"

/* Bitmap: 1 means free. Fenwick counts choose a rank in O(log(words)). */
#define MAX_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)
#define WORDS ((MAX_PAGES + 63) / 64)
static uint64 available[WORDS];
static uint32 counts[WORDS + 1];
static uint64 first, limit, npages, nfree;
extern char kernel_end[];

void
memory_zero(void *dst, uint64 n)
{
  uint8 *p = dst;
  while (n--) *p++ = 0;
}

void
memory_copy(void *dst, const void *src, uint64 n)
{
  uint8 *d = dst;
  const uint8 *s = src;
  while (n--) *d++ = *s++;
}

static void
count_add(uint64 word, int delta)
{
  uint64 i;
  for (i = word + 1; i <= WORDS; i += i & -i)
    counts[i] += delta;
}

static void __attribute__((noreturn))
allocator_fault(void)
{
  printf("kfree: invalid or duplicate physical page\n");
  for (;;) asm volatile("wfi");
}

void
kinit(void)
{
  uint64 i;
  first = PGROUNDUP((uint64)kernel_end);
  limit = PHYSTOP - (uint64)LAB3_MEMCAP_DELTA * PGSIZE;
  npages = limit > first ? (limit - first) / PGSIZE : 0;
  nfree = npages;
  memory_zero(available, sizeof(available));
  memory_zero(counts, sizeof(counts));
  for (i = 0; i < npages; i++) {
    available[i / 64] |= 1UL << (i % 64);
    count_add(i / 64, 1);
  }
}

void *
kalloc(void)
{
  uint64 rank, word = 0, step, bit, value;
  void *page;
  int enabled = intr_get();
  intr_off();
  if (!nfree) {
    if (enabled) intr_on();
    return 0;
  }
#if LAB3_ALLOC_ORDER == 0
  rank = 0;
#elif LAB3_ALLOC_ORDER == 1
  rank = nfree - 1;
#elif LAB3_ALLOC_ORDER == 2
  rank = (nfree - 1) / 2; /* Lower median of currently free addresses. */
#else
#error "invalid LAB3_ALLOC_ORDER"
#endif
  for (step = WORDS; step; step >>= 1) {
    uint64 next = word + step;
    if (next <= WORDS && counts[next] <= rank) {
      rank -= counts[next];
      word = next;
    }
  }
  value = available[word];
  for (bit = 0; bit < 64; bit++) {
    if (!(value & (1UL << bit))) continue;
    if (!rank) break;
    rank--;
  }
  available[word] &= ~(1UL << bit);
  count_add(word, -1);
  nfree--;
  page = (void *)(first + (word * 64 + bit) * PGSIZE);
  memory_zero(page, PGSIZE);
  if (enabled) intr_on();
  return page;
}

void
kfree(void *page)
{
  uint64 pa = (uint64)page, index, i;
  int enabled = intr_get();
  intr_off();
  if (pa < first || pa >= limit || (pa & (PGSIZE - 1)))
    allocator_fault();
  index = (pa - first) / PGSIZE;
  if (available[index / 64] & (1UL << (index % 64))) allocator_fault();
  for (i = 0; i < PGSIZE; i++) ((uint8 *)page)[i] = 1;
  available[index / 64] |= 1UL << (index % 64);
  count_add(index / 64, 1);
  nfree++;
  if (enabled) intr_on();
}

uint64 kfree_count(void) { return nfree; }
uint64 kpage_start(void) { return first; }
uint64 kpage_limit(void) { return limit; }
