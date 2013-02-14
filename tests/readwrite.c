#include "tests/lib.h"
#include "lib/debug.h"

int main( void )
{
  char buffer[1];
  char buffer_assert[] = "\nOK\n";

  int bytes_read = 0;
  int bytes_written = 0;
  
  bytes_read = syscall_read( 0, buffer, 1 );
  if( bytes_read == 1 ) {
    bytes_written = syscall_write( 1, buffer, 1 );
    if( bytes_written == 1 ) {
      syscall_write( 1, buffer_assert, 4 );
    }
  }
  syscall_halt( );  
  return 0;
}
