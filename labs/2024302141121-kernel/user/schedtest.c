#include "kernel/types.h"
#include "user/user.h"
int main(void)
{
  int pid = fork(), start = uptime(), status = -1;
  if (pid < 0) { printf("SCHEDTEST FAIL fork\n"); exit(1); }
  if (!pid) {
    if (pause(3) != 0) exit(1);
    printf("SCHED-CHILD\n");
    exit(6);
  }
  while (uptime() - start < 8) /* Keep the measured load in U mode for timer preemption. */
    for (volatile int i = 0; i < 1000000; i++);
  printf("SCHED-PARENT-WAIT\n");
  if (wait(&status) == pid && status == 6) printf("SCHEDTEST PASS\n");
  else printf("SCHEDTEST FAIL status=%d\n", status);
  exit(0);
}
