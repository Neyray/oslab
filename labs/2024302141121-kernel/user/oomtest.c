#include "kernel/types.h"
#include "user/user.h"
#define REQUIRE(condition) do { if (!(condition)) { printf("OOMTEST FAIL line=%d\n", __LINE__); exit(1); } } while (0)
int main(void)
{
  int total = 0, status, pid;
  char *base = sbrk(0), *p;
  while ((p = sbrk(65536)) != (char *)-1) { p[0] = 0x5a; total += 65536; }
  while ((p = sbrk(4096)) != (char *)-1) { p[0] = 0x5a; total += 4096; }
  REQUIRE(total > 7 * 4096 && fork() == -1 && exec("hi", 0) == -1 && base[0] == 0x5a);
  /* Free just enough for a partially constructed exec, then force rollback. */
  REQUIRE(sbrk(-5 * 4096) == base + total);
  REQUIRE(exec("hi", 0) == -1 && base[0] == 0x5a);
  REQUIRE(sbrk(5 * 4096) == base + total - 5 * 4096);
  REQUIRE(sbrk(4096) == (char *)-1); /* No leaked or spuriously created pages. */
  /* Seven pages reach fork's copy phase but cannot clone the whole image. */
  REQUIRE(sbrk(-7 * 4096) == base + total);
  REQUIRE(fork() == -1 && base[0] == 0x5a);
  REQUIRE(sbrk(7 * 4096) == base + total - 7 * 4096);
  REQUIRE(sbrk(4096) == (char *)-1 && sbrk(0) == base + total);
  REQUIRE(sbrk(-total) == base + total);
  pid = fork();
  if (!pid) exit(17);
  REQUIRE(pid > 0 && wait(&status) == pid && status == 17 && sbrk(0) == base);
  printf("OOMTEST PASS bytes=%d\n", total);
  exit(0);
}
