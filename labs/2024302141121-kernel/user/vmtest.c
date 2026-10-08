#include "kernel/types.h"
#include "user/user.h"
#define CHECK(condition) do { if (!(condition)) { printf("VMTEST FAIL line=%d\n", __LINE__); exit(1); } } while (0)
int main(void)
{
  int started = uptime();
  CHECK(pause(2) == 0 && uptime() - started >= 2);
  char *base = sbrk(0), *p = sbrk(8192);
  CHECK(p == base && sbrk(0) == base + 8192);
  char *cross = p + 4096 - 8;
  const char *message = "CROSS-PAGE-OK!\n";
  int n = strlen(message);
  memmove(cross, message, n);
  CHECK(write(1, cross, n) == n);
  printf("VMTEST input-ready\n");
  CHECK(read(0, cross, 16) == 12 && memcmp(cross, "cross-input\n", 12) == 0);
  CHECK(read(0, (void *)0, 1) == -1); /* Must reject before waiting for input. */
  CHECK(write(1, (void *)0x3fffffe000UL, 1) == -1);
  CHECK(write(1, base + 8192 - 1, 2) == -1);
  CHECK(write(1, (void *)~0UL, 2) == -1);
  CHECK(sbrk(1) == base + 8192 && sbrk(1) == base + 8193);
  CHECK(sbrk(-2) == base + 8194 && sbrk(0) == base + 8192);
  CHECK(sbrk(-8193) == (char *)-1);
  CHECK(sbrklazy(1) == base + 8192);
  CHECK(sbrk(-1) == base + 8193);
  p[0] = 11;
  int pid = fork(), status = -99;
  CHECK(pid >= 0);
  if (!pid) { p[0] = 22; exit(7); }
  CHECK(wait((int *)0) == pid); /* NULL is explicitly allowed. */
  CHECK(p[0] == 11);
  pid = fork();
  CHECK(pid >= 0);
  if (!pid) exit(9);
  CHECK(wait((int *)1) == -1); /* Code is not writable, child remains waitable. */
  CHECK(wait(&status) == pid && status == 9);
  CHECK(exec("not-present", 0) == -1 && p[0] == 11);
  CHECK(sbrk(-8192) == base + 8192 && sbrk(0) == base);
  printf("VMTEST PASS\n");
  exit(0);
}
