#include "types.h"
#include "course_sid.h"
#include "proc.h"
#include "defs.h"

extern void console_init(void);
extern void console_checksum_reset(void);
extern uint64 console_checksum_value(void);
extern void console_checksum_pause(void);

#define INT_MIN_VALUE (-2147483647 - 1)
#define INT_MAX_VALUE 2147483647

static const char long_test_text[] =
  "0123456789012345678901234567890123456789"
  "0123456789012345678901234567890123456789";

void __attribute__((noreturn))
main(void)
{
  uint64 sum;

  console_init();
  console_checksum_reset();

  printf("OSLAB1 sid=%lu mod97=%x\n", (uint64)COURSE_SID,
         (uint)(COURSE_SID % 97UL));
  printf("selftest zero=%d neg=%d max=%d empty='%s' hex=%x long=%s\n",
         0, INT_MIN_VALUE, INT_MAX_VALUE, "", 0xffffffffU,
         long_test_text);

#if LAB1_BANNER_PROTOCOL == 2
  sum = console_checksum_value();
  console_checksum_pause();
  printf("[chk=%lu]\n", sum);
#else
  sum = 0;
  (void)sum;
  console_checksum_pause();
#endif

#ifdef LAB1_PRINTF_EXTRA_TEST
  /* Built only by tests/lab1_selftest.py; the default banner is unchanged. */
  printf("extra ptr=%p chr=%c pct=%% lmin=%ld lmax=%lu null=%s "
         "hexzero=%x neg1=%d unknown=%q\n",
         (void *)0x80000000UL, 'Z', (-9223372036854775807L - 1),
         0xffffffffffffffffUL, (char *)0, 0U, -1);
#endif

  vm_init();
  trap_init();
  proc_init();
  printf("lab2 ready: tick=%d buffer=%d semantics=%d\n", LAB2_TICK,
         LAB2_BUF_SIZE, LAB2_BUF_SEMANTICS);
  proc_enter_user();
}
