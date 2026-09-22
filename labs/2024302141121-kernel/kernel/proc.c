#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"

#define PTE_A (1L << 6)
#define PTE_D (1L << 7)
#define USER_PAGES (LAB2_USER_SIZE / PGSIZE)

extern char trampoline[];
extern char _uprog_table[];

static pte_t kernel_root[512] __attribute__((aligned(PGSIZE)));
static pte_t kernel_high_l1[512] __attribute__((aligned(PGSIZE)));
static pte_t kernel_high_l0[512] __attribute__((aligned(PGSIZE)));

static pte_t user_roots[LAB2_NPROC][512] __attribute__((aligned(PGSIZE)));
static pte_t user_low_l1[LAB2_NPROC][512] __attribute__((aligned(PGSIZE)));
static pte_t user_low_l0[LAB2_NPROC][512] __attribute__((aligned(PGSIZE)));
static pte_t user_high_l1[LAB2_NPROC][512] __attribute__((aligned(PGSIZE)));
static pte_t user_high_l0[LAB2_NPROC][512] __attribute__((aligned(PGSIZE)));

static uint8 user_memory[LAB2_NPROC][LAB2_USER_SIZE]
  __attribute__((aligned(PGSIZE)));
static uint8 trapframe_pages[LAB2_NPROC][PGSIZE]
  __attribute__((aligned(PGSIZE)));
static uint8 kernel_stacks[LAB2_NPROC][LAB2_KSTACK_SIZE]
  __attribute__((aligned(16)));

static struct proc processes[LAB2_NPROC];
static struct proc *current;
static uint64 kernel_satp;
static int next_pid = 1;

typedef char trapframe_a0_offset_must_match[
  __builtin_offsetof(struct trapframe, a0) == 112 ? 1 : -1];
typedef char trapframe_t6_offset_must_match[
  __builtin_offsetof(struct trapframe, t6) == 280 ? 1 : -1];

static void
memory_zero(void *destination, uint64 length)
{
  uint8 *bytes = destination;
  while (length-- != 0)
    *bytes++ = 0;
}

static void
memory_copy(void *destination, const void *source, uint64 length)
{
  uint8 *out = destination;
  const uint8 *in = source;
  while (length-- != 0)
    *out++ = *in++;
}

static int
string_equal(const char *left, const char *right)
{
  while (*left != '\0' && *left == *right) {
    left++;
    right++;
  }
  return *left == *right;
}

static uint64
align_up_8(uint64 value)
{
  return (value + 7) & ~7UL;
}

static uint64
trampoline_page(void)
{
  return PGROUNDDOWN((uint64)trampoline);
}

static void
setup_user_pagetable(int slot)
{
  pte_t *root = user_roots[slot];
  pte_t *low_l1 = user_low_l1[slot];
  pte_t *low_l0 = user_low_l0[slot];
  pte_t *high_l1 = user_high_l1[slot];
  pte_t *high_l0 = user_high_l0[slot];
  uint64 page;

  memory_zero(root, PGSIZE);
  memory_zero(low_l1, PGSIZE);
  memory_zero(low_l0, PGSIZE);
  memory_zero(high_l1, PGSIZE);
  memory_zero(high_l0, PGSIZE);

  root[PX(2, 0)] = PA2PTE(low_l1) | PTE_V;
  low_l1[PX(1, 0)] = PA2PTE(low_l0) | PTE_V;
  for (page = 0; page < USER_PAGES; page++) {
    uint64 physical = (uint64)&user_memory[slot][page * PGSIZE];
    low_l0[page] = PA2PTE(physical) |
                   PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D;
  }

  root[PX(2, TRAMPOLINE)] = PA2PTE(high_l1) | PTE_V;
  high_l1[PX(1, TRAMPOLINE)] = PA2PTE(high_l0) | PTE_V;
  high_l0[PX(0, TRAMPOLINE)] = PA2PTE(trampoline_page()) |
                               PTE_V | PTE_R | PTE_X | PTE_A;
  high_l0[PX(0, TRAPFRAME)] = PA2PTE(trapframe_pages[slot]) |
                              PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;

  processes[slot].pagetable = root;
  processes[slot].user_satp = MAKE_SATP(root);
}

void
vm_init(void)
{
  uint64 tramp = trampoline_page();

  memory_zero(kernel_root, PGSIZE);
  memory_zero(kernel_high_l1, PGSIZE);
  memory_zero(kernel_high_l0, PGSIZE);

  /* Two aligned 1 GiB leaves cover MMIO and all kernel RAM. */
  kernel_root[0] = PA2PTE(0) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
  kernel_root[2] = PA2PTE(KERNBASE) |
                   PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;

  kernel_root[PX(2, TRAMPOLINE)] = PA2PTE(kernel_high_l1) | PTE_V;
  kernel_high_l1[PX(1, TRAMPOLINE)] = PA2PTE(kernel_high_l0) | PTE_V;
  kernel_high_l0[PX(0, TRAMPOLINE)] = PA2PTE(tramp) |
                                      PTE_V | PTE_R | PTE_X | PTE_A;

  kernel_satp = MAKE_SATP(kernel_root);
  w_satp(kernel_satp);
  sfence_vma();
}

uint64
kernel_satp_value(void)
{
  return kernel_satp;
}

struct proc *
myproc(void)
{
  return current;
}

static void
prepare_slot(int slot)
{
  struct proc *process = &processes[slot];

  memory_zero(process, sizeof(*process));
  memory_zero(user_memory[slot], LAB2_USER_SIZE);
  memory_zero(trapframe_pages[slot], PGSIZE);
  process->trapframe = (struct trapframe *)trapframe_pages[slot];
  process->memory = user_memory[slot];
  process->kernel_stack_top =
    (uint64)&kernel_stacks[slot][LAB2_KSTACK_SIZE];
  setup_user_pagetable(slot);
}

int
proc_copyin(void *destination, uint64 source, uint64 length)
{
  if (current == 0 || source > LAB2_USER_SIZE ||
      length > LAB2_USER_SIZE - source)
    return -1;
  memory_copy(destination, current->memory + source, length);
  return 0;
}

int
proc_copyout(uint64 destination, const void *source, uint64 length)
{
  if (current == 0 || destination > LAB2_USER_SIZE ||
      length > LAB2_USER_SIZE - destination)
    return -1;
  memory_copy(current->memory + destination, source, length);
  return 0;
}

int
proc_copyinstr(char *destination, uint64 source, uint64 maximum)
{
  uint64 used;

  if (maximum == 0 || source >= LAB2_USER_SIZE)
    return -1;
  for (used = 0; used + 1 < maximum && source + used < LAB2_USER_SIZE;
       used++) {
    char byte = (char)current->memory[source + used];
    destination[used] = byte;
    if (byte == '\0')
      return 0;
  }
  destination[used] = '\0';
  return -1;
}

int
proc_exec(const char *requested_name)
{
  char name[32];
  uint64 length = 0;
  uint8 *cursor = (uint8 *)_uprog_table;

  while (requested_name[length] != '\0' && requested_name[length] != '\n' &&
         requested_name[length] != '\r' && length + 1 < sizeof(name)) {
    name[length] = requested_name[length];
    length++;
  }
  name[length] = '\0';

  for (;;) {
    uint64 start = *(uint64 *)(cursor + 0);
    uint64 end = *(uint64 *)(cursor + 8);
    const char *entry_name;
    uint64 image_size;

    if (start == 0 && end == 0)
      return -1;
    entry_name = (const char *)(cursor + 16);
    image_size = end - start;
    if (string_equal(name, entry_name)) {
      if (end < start || image_size + PGSIZE > LAB2_USER_SIZE)
        return -1;
      memory_zero(current->memory, LAB2_USER_SIZE);
      memory_copy(current->memory, (const void *)start, image_size);
      memory_zero(current->trapframe, sizeof(*current->trapframe));
      current->trapframe->epc = 0;
      current->trapframe->sp = LAB2_USER_SIZE - 16;
      return 0;
    }

    while (*entry_name++ != '\0')
      ;
    cursor = (uint8 *)align_up_8((uint64)entry_name);
  }
}

void
proc_init(void)
{
  int slot;

  for (slot = 0; slot < LAB2_NPROC; slot++)
    processes[slot].state = PROC_UNUSED;

  prepare_slot(0);
  current = &processes[0];
  current->pid = next_pid++;
  current->parent_pid = 0;
  current->state = PROC_RUNNING;
  if (proc_exec("sh") < 0) {
    printf("lab2: embedded sh not found\n");
    for (;;)
      asm volatile("wfi");
  }
}

int
proc_fork(void)
{
  int slot;
  struct proc *child;

  for (slot = 1; slot < LAB2_NPROC; slot++)
    if (processes[slot].state == PROC_UNUSED)
      break;
  if (slot == LAB2_NPROC)
    return -1;

  child = &processes[slot];
  prepare_slot(slot);
  memory_copy(child->memory, current->memory, LAB2_USER_SIZE);
  memory_copy(child->trapframe, current->trapframe,
              sizeof(*child->trapframe));
  child->pid = next_pid++;
  child->parent_pid = current->pid;
  child->state = PROC_RUNNABLE;
  child->trapframe->a0 = 0;
  return child->pid;
}

int
proc_wait(void)
{
  int slot;

  for (slot = 0; slot < LAB2_NPROC; slot++) {
    if (processes[slot].state == PROC_RUNNABLE &&
        processes[slot].parent_pid == current->pid) {
      current->state = PROC_WAITING;
      current = &processes[slot];
      current->state = PROC_RUNNING;
      return 0;
    }
  }
  return -1;
}

void
proc_exit(int status)
{
  struct proc *departing = current;
  struct proc *parent = 0;
  int slot;

  departing->exit_status = status;
  for (slot = 0; slot < LAB2_NPROC; slot++) {
    if (processes[slot].pid == departing->parent_pid &&
        processes[slot].state == PROC_WAITING) {
      parent = &processes[slot];
      break;
    }
  }

  if (parent == 0) {
    printf("lab2: pid %d exited without a waiting parent\n", departing->pid);
    for (;;)
      asm volatile("wfi");
  }

  parent->trapframe->a0 = departing->pid;
  parent->state = PROC_RUNNING;
  departing->state = PROC_UNUSED;
  current = parent;
}

void
proc_enter_user(void)
{
  usertrapret();
}
