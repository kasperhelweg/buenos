/*
 * Process startup.
 *
 * Copyright (C) 2003 Juha Aatrokoski, Timo Lilja,
 *   Leena Salmela, Teemu Takanen, Aleksi Virtanen.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: process.h,v 1.4 2003/05/16 10:13:55 ttakanen Exp $
 *
 */

#ifndef BUENOS_PROC_PROCESS
#define BUENOS_PROC_PROCESS

/* typedefs for process */
typedef int process_id_t;
/* process name, currently restricted to 20 chars + escape */
typedef const char* process_name_t;

/* filedescriptor id type */
typedef int filedescriptor_id_t;

#define USERLAND_STACK_TOP 0x7fffeffc

#define PROCESS_PTABLE_FULL  -1
#define PROCESS_ILLEGAL_JOIN -2

#define PROCESS_MAX_PROCESSES 32

/* state enum */
typedef enum {
    PROCESS_FREE,
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_SLEEPING,
    PROCESS_NONREADY,
    PROCESS_DYING,
    PROCESS_ZOMBIE
} process_state_t;;

/* PCB */
typedef struct process_control_block_t {
  /* the process id and name */
  process_id_t pid;
  process_name_t name;
  /* state of the process */
  process_state_t state;
  
  /* parent process stuff */
  struct process_control_block_t* parent;
  
  /* *************** */
  /* children( choose a datasctructure for these ) */
  /* *************** */

} process_control_block_t;

/* choose a datastrucure to hold files */ 
/* FD block */
typedef struct process_file_descriptor_t {
  /* the filedescriptor id*/
  filedescriptor_id_t fid;

} process_file_descriptor_t;

void process_start( process_id_t pid );

/* Initialize the process table.  This must be called during kernel
   startup before any other process-related calls. */
void process_init( void );

/* Run process in a new thread. Returns the PID of the new process. */
process_id_t process_spawn( const char* executable );

/* Stop the process and the thread it runs in. Sets the return value as well */
void process_finish( int retval );

/* Wait for the given process to terminate, returning its return value. This
 * will also mark its process table entry as free.
 * Only works on child processes */
int process_join( process_id_t pid );

/* Return PID of current process. */
process_id_t process_get_current_process( void );

/* Return PCB of current process. */
process_control_block_t* process_get_current_process_entry( void );

#endif
