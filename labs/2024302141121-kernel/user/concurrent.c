/* Exercise the supplied spin and sh together without modifying either gift. */
#include "kernel/types.h"
#include "user/user.h"
int main(void)
{
  int pid = fork();
  if (pid < 0) { printf("CONCURRENT FAIL fork\n"); exit(1); }
  if (!pid) { exec("spin", 0); exit(1); }
  exec("sh", 0);
  printf("CONCURRENT FAIL exec\n");
  exit(1);
}
