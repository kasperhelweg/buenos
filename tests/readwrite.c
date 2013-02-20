#include "tests/lib.h"
#include "lib/debug.h"

int main( void )
{
  char buffer[1];
  char buffer_newline[] = "\n";
  
  int bytes_read = 0;
  int bytes_written = 0;
  
  bytes_read = syscall_read( stdin, buffer, 1 );
  
  if( bytes_read > 0 ) {
    bytes_written = syscall_write( stdout, buffer, 1 );
    if( bytes_written > 0 ) {
      syscall_write( stdout, buffer_newline, 1 );
    }
  }
  /* syscall exit() not implemented */
  syscall_halt( );  
  return 0;
}
