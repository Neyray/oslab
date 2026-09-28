/* Self-test T2-1 (boundary): reads sized exactly LAB2_BUF_SIZE.
 * Driven by tests/lab2_selftest.py, which types three lines after each
 * prompt: a line that fills the ring exactly, an overlong line, then "ok".
 */
#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

static char buf[LAB2_BUF_SIZE];

int
main(void)
{
  int n, total = 0, calls = 0, bad = 0;

  /* Case 1: LAB2_BUF_SIZE - 1 letters plus '\n' fill the ring exactly. */
  printf("readcap: send %d-byte line\n", LAB2_BUF_SIZE);
  n = read(0, buf, LAB2_BUF_SIZE);
  if (n != LAB2_BUF_SIZE || buf[n - 1] != '\n') {
    printf("readcap: exact line returned %d\n", n);
    bad++;
  }

  /* Case 2: an overlong line may lose letters, never its '\n'. */
  printf("readcap: send overlong line\n");
  do {
    n = read(0, buf, LAB2_BUF_SIZE);
    calls++;
    if (n <= 0 || n > LAB2_BUF_SIZE) {
      printf("readcap: overlong read returned %d\n", n);
      bad++;
      break;
    }
    total += n;
  } while (buf[n - 1] != '\n' && calls < 8);
  if (n <= 0 || buf[n - 1] != '\n' || total < LAB2_BUF_SIZE) {
    printf("readcap: overlong line total=%d calls=%d\n", total, calls);
    bad++;
  }

  /* Case 3: the ring is still consistent after overflow. */
  printf("readcap: send short line\n");
  n = read(0, buf, LAB2_BUF_SIZE);
  if (n != 3 || memcmp(buf, "ok\n", 3) != 0) {
    printf("readcap: short line returned %d\n", n);
    bad++;
  }

  printf("READCAP %s overlong_total=%d overlong_calls=%d\n",
         bad ? "FAIL" : "PASS", total, calls);
  exit(bad ? 1 : 0);
}
