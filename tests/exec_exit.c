/*
 * Userland exec test
 */

#include "tests/lib.h"

#define MAX_PROC 10
static const char prog[] = "[root]exit"; /* The program to start. */

int main(void)
{
  int ret;
  int i;

  int children[MAX_PROC];
  
  for(i=1; i < MAX_PROC; i++){
    printf("Spawning child %d\n", i);
    children[i] = syscall_exec(prog);
   }

  for(i=1; i < MAX_PROC; i++){
    ret = (char)syscall_join(children[i]);
    printf("Child joined with status: %d\n", ret);
  }
  
  for(i=1; i < MAX_PROC; i++){
    printf("Spawning child %d\n", i);
    children[i] = syscall_exec(prog);
   }

    for(i=1; i < MAX_PROC; i++){
    ret = (char)syscall_join(children[i]);
    printf("Child joined with status: %d\n", ret);
  }


  syscall_halt();
  return 0;
}
