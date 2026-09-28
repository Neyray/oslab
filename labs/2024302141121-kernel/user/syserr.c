/* Self-test T2-3: illegal syscall numbers return -1, and so do the other
 * requests the kernel must reject; the process keeps running afterwards.
 *
 * main is deliberately the only function here. exec starts a flat binary
 * at address 0 and ENTRY(main) does not survive objcopy -O binary, while
 * GCC is free to emit static helpers ahead of main. Helpers are macros.
 */
#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"

#define USER_LIMIT 0x10000UL  /* LAB2_USER_SIZE: 64 KiB per process */

/* Issue ecall with an arbitrary number, bypassing the usys.pl stubs. */
#define ECALL_RAW(number) ({                              \
  register long a0_ asm("a0") = 0;                        \
  register long a7_ asm("a7") = (number);                 \
  asm volatile("ecall" : "+r"(a0_) : "r"(a7_) : "memory"); \
  (int)a0_;                                               \
})

#define EXPECT(what, got, want) do {                                  \
  int got_ = (got);                                                   \
  if (got_ != (want)) {                                               \
    printf("syserr: %s returned %d, want %d\n", what, got_, want);   \
    failures++;                                                       \
  }                                                                   \
} while (0)

int
main(void)
{
  int failures = 0;
  char byte = 'x';
  char *argv[] = { "nosuch", 0 };

  /* Syscall numbers just outside the defined 1..22 range, and one inside
   * it that this lab does not implement. */
  EXPECT("syscall 0", ECALL_RAW(0), -1);
  EXPECT("syscall 23", ECALL_RAW(SYS_sync + 1), -1);
  EXPECT("syscall -1", ECALL_RAW(-1), -1);
  EXPECT("unimplemented open", open("x", 0), -1);

  /* Descriptor and length validation. */
  EXPECT("write fd 0", write(0, &byte, 1), -1);
  EXPECT("write fd 3", write(3, &byte, 1), -1);
  EXPECT("write length -1", write(1, &byte, -1), -1);
  EXPECT("read fd 1", read(1, &byte, 1), -1);
  EXPECT("read length 0", read(0, &byte, 0), 0);

  /* User pointers outside, or running past, the 64 KiB user image. */
  EXPECT("write beyond image", write(1, (char *)(2 * USER_LIMIT), 4), -1);
  EXPECT("write across end", write(1, (char *)(USER_LIMIT - 2), 8), -1);

  /* Process-level failures leave the caller intact. */
  EXPECT("exec missing program", exec("nosuch", argv), -1);
  EXPECT("wait without child", wait(0), -1);
  EXPECT("pause negative", pause(-1), -1);

  if (getpid() <= 0) {
    printf("syserr: getpid failed after error paths\n");
    failures++;
  }

  printf("SYSERR %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
  exit(failures ? 1 : 0);
}
