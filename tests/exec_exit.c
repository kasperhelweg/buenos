/*
 * Userland exec test
 */

#include "tests/lib.h"

static const char prog[] = "[root]exit"; /* The program to start. */

int main(void)
{
  uint32_t child;
  int ret;
  int i;

  printf("Starting program %s\n", prog);
  child = syscall_exec(prog);
  for (i=1 ; i<100000 ; i++) ;
  printf("Now joining child %d\n", child);
  ret = (char)syscall_join(child);
  printf("Child joined with status: %d\n", ret);
  syscall_halt();
  return 0;
}
