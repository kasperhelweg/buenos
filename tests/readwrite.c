#include "tests/lib.h"

int main( void )
{
  char* buffer[64];
  syscall_read(0, buffer, 63);

  syscall_halt();  
  return 0;
}
