#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"
int main(void)
{
  uint64 sp;
  asm volatile("mv %0, sp" : "=r"(sp));
  uint64 stackbase = sp & ~4095UL;
  int failures = 0;
  for (int page = 0; page < LAB3_GUARD_PAGES; page++) {
    for (int writing = 0; writing <= 1; writing++) {
      int pid = fork(), status = 0;
      if (pid < 0) { failures++; continue; }
      if (!pid) {
        volatile char *guard = (void *)(stackbase - 1 - page * 4096);
        if (writing) *guard = 1;
        else { volatile char value = *guard; (void)value; }
        exit(0);
      }
      if (wait(&status) != pid || status == 0) failures++;
    }
  }
  printf("GUARDTEST %s pages=%d failures=%d\n", failures ? "FAIL" : "PASS", LAB3_GUARD_PAGES, failures);
  exit(failures);
}
