#include "tests/lib.h"
#include "lib/debug.h"

int main( void )
{
  char buffer[1];
  
  syscall_read(0, buffer, 1);
  syscall_write(1, buffer, 1);

  buffer[0] = '\n';
  syscall_write(1, buffer, 1);
  
  syscall_halt();  
  return 0;
}
