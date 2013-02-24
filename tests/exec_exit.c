/*
 * Userland exec test
 */

#include "tests/lib.h"

static const char prog[] = "[root]exit"; /* The program to start. */

int main(void)
{
  uint32_t child1;
  uint32_t child2;
  uint32_t child3;
  uint32_t child4;
  int ret;
  int i;

  printf("Starting program %s\n", prog);
  child1 = syscall_exec(prog);
  printf("Starting program %s\n", prog);   
  child2 = syscall_exec(prog);
  printf("Starting program %s\n", prog);  
  child3 = syscall_exec(prog);
  printf("Starting program %s\n", prog);
  child4 = syscall_exec(prog);
  
  /* wait */
  for (i=1 ; i<10000000 ; i++) ;
  
  printf("Now joining child %d\n", child1);
  ret = (char)syscall_join(child1);
  printf("Child joined with status: %d\n", ret);
  
  printf("Now joining child %d\n", child2);
  ret = (char)syscall_join(child2);
  printf("Child joined with status: %d\n", ret);
  
  printf("Now joining child %d\n", child3);
  ret = (char)syscall_join(child3);
  printf("Child joined with status: %d\n", ret);
  
  printf("Now joining child %d\n", child4);
  ret = (char)syscall_join(child4);
  printf("Child joined with status: %d\n", ret);

  syscall_halt();
  return 0;
}
