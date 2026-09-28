/* Self-test T2-2: read(0, &c, 1) on an empty input buffer.
 * It must block (neither 0 nor -1) until input arrives. With
 * LAB2_BUF_SEMANTICS=1 one key is enough; with 0 it waits for Enter.
 * tests/lab2_selftest.py stays silent first, then sends "q" without Enter,
 * then Enter, and checks when the read returned.
 */
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char byte = 0;
  int n;

  printf("readempty: ready\n");
  n = read(0, &byte, 1);
  printf("\nREADEMPTY n=%d byte=%d\n", n, byte);

  /* Consume the rest of the line so the shell does not execute it. */
  while (n == 1 && byte != '\n')
    n = read(0, &byte, 1);
  exit(0);
}
