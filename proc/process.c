/*
 * Process startup.
 *
 * Copyright (C) 2003-2005 Juha Aatrokoski, Timo Lilja,
 *       Leena Salmela, Teemu Takanen, Aleksi Virtanen.
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
 * $Id: process.c,v 1.11 2007/03/07 18:12:00 ttakanen Exp $
 *
 */
#include "lib/debug.h"
#include "lib/libc.h"
#include "proc/process.h"
#include "proc/elf.h"
#include "kernel/thread.h"
#include "kernel/assert.h"
#include "kernel/interrupt.h"
#include "kernel/config.h"
#include "kernel/sleepq.h"
#include "fs/vfs.h"
#include "drivers/yams.h"
#include "vm/vm.h"
#include "vm/pagepool.h"

#include "kernel/lock_cond.h"

/* process_tabel spinlock */
static spinlock_t pt_slock;

/* internal prototypes */
process_id_t process_get_free_table_slot( void );
void process_wrapper( process_id_t pid );

/** @name Process startup
 *
 * This module contains facilities for managing userland process.
 */

process_control_block_t process_table[PROCESS_MAX_PROCESSES];

/**
 * Starts one userland process. The thread calling this function will
 * be used to run the process and will therefore never return from
 * this function. This function asserts that no errors occur in
 * process startup (the executable file exists and is a valid ecoff
 * file, enough memory is available, file operations succeed...).
 * Therefore this function is not suitable to allow startup of
 * arbitrary processes.
 *
 * @executable The name of the executable to be run in the userland
 * process
 */
void process_start( process_id_t pid )
{
  /* name of executable */
  const char* executable;
  thread_table_t* my_entry;
  pagetable_t* pagetable;
  uint32_t phys_page;
  context_t user_context;
  uint32_t stack_bottom;
  elf_info_t elf;
  openfile_t file;

  int i;

  interrupt_status_t intr_status;
  
  /* initial thread_entry setup */
  my_entry = thread_get_current_thread_entry( );
 
  /* set pid and executable name */
  my_entry->process_id = pid;
  executable = process_table[pid].name;
  
  /* If the pagetable of this thread is not NULL, we are trying to
     run a userland process for a second time in the same thread.
     This is not possible. */
  KERNEL_ASSERT(my_entry->pagetable == NULL);

  pagetable = vm_create_pagetable(thread_get_current_thread());
  KERNEL_ASSERT(pagetable != NULL);
  
  intr_status = _interrupt_disable();
  my_entry->pagetable = pagetable;
  _interrupt_set_state(intr_status);

  file = vfs_open((char*)executable);
  
  /* Make sure the file existed and was a valid ELF file */
  KERNEL_ASSERT(file >= 0);
  KERNEL_ASSERT(elf_parse_header(&elf, file));

  /* Trivial and naive sanity check for entry point: */
  KERNEL_ASSERT(elf.entry_point >= PAGE_SIZE);

  /* Calculate the number of pages needed by the whole process
     (including userland stack). Since we don't have proper tlb
     handling code, all these pages must fit into TLB. */
  KERNEL_ASSERT(elf.ro_pages + elf.rw_pages + CONFIG_USERLAND_STACK_SIZE
                <= _tlb_get_maxindex() + 1);

  /* Allocate and map stack */
  for(i = 0; i < CONFIG_USERLAND_STACK_SIZE; i++) {
    phys_page = pagepool_get_phys_page();
    KERNEL_ASSERT(phys_page != 0);
    vm_map(my_entry->pagetable, phys_page, 
           (USERLAND_STACK_TOP & PAGE_SIZE_MASK) - i*PAGE_SIZE, 1);
  }

  /* Allocate and map pages for the segments. We assume that
     segments begin at page boundary. (The linker script in tests
     directory creates this kind of segments) */
  for(i = 0; i < (int)elf.ro_pages; i++) {
    phys_page = pagepool_get_phys_page();
    KERNEL_ASSERT(phys_page != 0);
    vm_map(my_entry->pagetable, phys_page, 
           elf.ro_vaddr + i*PAGE_SIZE, 1);
  }

  for(i = 0; i < (int)elf.rw_pages; i++) {
    phys_page = pagepool_get_phys_page();
    KERNEL_ASSERT(phys_page != 0);
    vm_map(my_entry->pagetable, phys_page, 
           elf.rw_vaddr + i*PAGE_SIZE, 1);
  }

  /* Put the mapped pages into TLB. Here we again assume that the
     pages fit into the TLB. After writing proper TLB exception
     handling this call should be skipped. */
  intr_status = _interrupt_disable();
  tlb_fill(my_entry->pagetable);
  _interrupt_set_state(intr_status);
    
  /* Now we may use the virtual addresses of the segments. */

  /* Zero the pages. */
  memoryset((void *)elf.ro_vaddr, 0, elf.ro_pages*PAGE_SIZE);
  memoryset((void *)elf.rw_vaddr, 0, elf.rw_pages*PAGE_SIZE);

  stack_bottom = (USERLAND_STACK_TOP & PAGE_SIZE_MASK) - 
    (CONFIG_USERLAND_STACK_SIZE-1)*PAGE_SIZE;
  memoryset((void *)stack_bottom, 0, CONFIG_USERLAND_STACK_SIZE*PAGE_SIZE);

  /* Copy segments */

  if (elf.ro_size > 0) {
    /* Make sure that the segment is in proper place. */
    KERNEL_ASSERT(elf.ro_vaddr >= PAGE_SIZE);
    KERNEL_ASSERT(vfs_seek(file, elf.ro_location) == VFS_OK);
    KERNEL_ASSERT(vfs_read(file, (void *)elf.ro_vaddr, elf.ro_size)
                  == (int)elf.ro_size);
  }

  if (elf.rw_size > 0) {
    /* Make sure that the segment is in proper place. */
    KERNEL_ASSERT(elf.rw_vaddr >= PAGE_SIZE);
    KERNEL_ASSERT(vfs_seek(file, elf.rw_location) == VFS_OK);
    KERNEL_ASSERT(vfs_read(file, (void *)elf.rw_vaddr, elf.rw_size)
                  == (int)elf.rw_size);
  }

  /* Set the dirty bit to zero (read-only) on read-only pages. */
  for(i = 0; i < (int)elf.ro_pages; i++) {
    vm_set_dirty(my_entry->pagetable, elf.ro_vaddr + i*PAGE_SIZE, 0);
  }

  /* Insert page mappings again to TLB to take read-only bits into use */
  intr_status = _interrupt_disable();
  tlb_fill(my_entry->pagetable);
  _interrupt_set_state(intr_status);

  /* Initialize the user context. (Status register is handled by
     thread_goto_userland) */
  memoryset(&user_context, 0, sizeof(user_context));
  user_context.cpu_regs[MIPS_REGISTER_SP] = USERLAND_STACK_TOP;
  user_context.pc = elf.entry_point;

  if( process_table[pid].state == PROCESS_READY ){
    process_table[pid].state = PROCESS_RUNNING;
    thread_goto_userland(&user_context);
  } 
  KERNEL_PANIC("thread_goto_userland failed.");
}

void process_init( void ) 
{
  int pid;
  /* set the spinclok to free*/
  spinlock_reset(&pt_slock);
  /* mark each process as free. 
   * no need to lock the table, since no threads are running */
  for( pid = 0; pid < PROCESS_MAX_PROCESSES; pid++ ){
    process_table[pid].state = PROCESS_FREE;
    process_table[pid].parent = NULL;      
    /* init children datastructure */
    process_table[pid].left_child = NULL;
    process_table[pid].right_child = NULL;
  }
}

void process_init_process( const char* executable ) 
{  
  process_control_block_t* init_process;
  
  init_process = &process_table[0];
  /* this can only be called from startup_thread */
  if( init_process->state == PROCESS_FREE && thread_get_current_thread( ) == 1 ) {      
     /* set entries in PCB. no need to lock the table. */
    init_process->tid = 1;     
    /* set name */
    stringcopy(init_process->name, executable, PROCESS_MAX_EXEC_CHARS);
    /* set state */
    init_process->state = PROCESS_READY;
  } else { 
    KERNEL_PANIC( "Create initial process failed!" );
  }
  process_start( 0 );
}

process_id_t process_spawn( const char* executable ) 
{ 
  process_control_block_t* current_process;
  process_control_block_t* child_process;
  
  process_id_t child_pid;
  TID_t child_tid;
 
  /* get the current process */
  current_process = &process_table[process_get_current_process( )];
  /* get free slot in process_table */
  child_pid = process_get_free_table_slot( );
  
  /* create a new thread and run it if process creation was succesfull */
  if( child_pid > 0 ){
 
    child_process = &process_table[child_pid];
    child_tid = thread_create((void (*)(uint32_t))&process_wrapper, (uint32_t)child_pid);
  
    /* no need to lock the table */
    child_process->tid = child_tid;
    stringcopy(child_process->name, executable, PROCESS_MAX_EXEC_CHARS);
    child_process->state = PROCESS_READY;

    /* add child */
    child_process->parent = current_process;
    child_process->left_child = current_process->right_child;
    current_process->right_child = child_process;

    /* run thread */
    thread_run( child_tid );   
  } else {
    /* do stuff. no more processes alloud*/
    KERNEL_PANIC( "Process allocation failed!" );
  }
  return child_pid; 
}

/* Stop the process and the thread it runs in. Sets the return value as well */
void process_finish( int retval ) 
{   
  thread_table_t* current_thread_entry;
  process_control_block_t* current_process;
  process_id_t pid;

  /* pointers to manipulate child/parent structure */
  process_control_block_t* walker;
  process_control_block_t* lastnode;

  pid = process_get_current_process( );
  if( pid != 0 ) {
    /* get current process and thread */
    current_process = &process_table[pid];
    current_thread_entry = thread_get_current_thread_entry( );
    
    interrupt_status_t intr_status;
    intr_status = _interrupt_disable( );
    spinlock_acquire( &pt_slock );
    /*==========LOCKED==========*/
    
    /* reorganize children.  might need to be locked...not sure. */
    /* remove link from parent to this. parent points to next immediate child */  
    (*(current_process->parent)).right_child = current_process->left_child;
    /* any children? */
    if( current_process->right_child != NULL ){ 
      walker = current_process->right_child;
      /* reparent all children to the init procees */
      while( walker != NULL ){
        walker->parent = process_table;
        lastnode = walker; walker = walker->left_child;
      }
      /* splice trees together */
      lastnode->left_child = process_table[0].right_child;
      process_table[0].right_child = current_process->right_child; 
    }

    /* set return value */
    current_process->return_code = retval;
    /* the process becomes a zombie process. 
       parent must call wait() or join() */
    current_process->state = PROCESS_ZOMBIE;  
    /* wake sleeping resource(s) */
    sleepq_wake( current_process );
    /*==========LOCKED==========*/
    spinlock_release( &pt_slock );  
    _interrupt_set_state( intr_status );
       /* do magic cleanup code and finish thread */
    vm_destroy_pagetable( current_thread_entry->pagetable );
    current_thread_entry->pagetable = NULL;
    
    thread_finish( );  
  } else {
    /* initial process exited...do stuff*/
    KERNEL_PANIC( "Initial process exited. Not sure what to do...so panic!" );
  }   
}

int process_join( process_id_t pid ) 
{
  int retval;  
  process_control_block_t* current_process;
  process_control_block_t* join_process;
    
  /* get current and join processes */
  current_process = &process_table[process_get_current_process( )];
  join_process = &process_table[pid];

  interrupt_status_t intr_status;
  intr_status = _interrupt_disable( );
  spinlock_acquire( &pt_slock );
  /*==========LOCKED==========*/
  while( join_process->state != PROCESS_ZOMBIE ){
    sleepq_add( join_process );
    /* this is weid... threade is always running it appears */
    current_process->state = thread_get_current_thread_entry( )->state;
    spinlock_release( &pt_slock );
    thread_switch( );
    spinlock_acquire( &pt_slock );  
  } 
  /* get return value */
  retval = join_process->return_code;
  /* clean up join process */
  join_process->parent = NULL;
  join_process->left_child = NULL;
  join_process->right_child = NULL;
  join_process->state = PROCESS_DEAD;
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_set_state( intr_status );
  
  return retval; /* return childs exit code*/
}

process_id_t process_get_current_process( void )
{
  return thread_get_current_thread_entry()->process_id; 
}

process_control_block_t* process_get_current_process_entry( void )
{
  return &process_table[process_get_current_process()];
}

process_control_block_t* process_get_process_entry( process_id_t pid ) {
  return &process_table[pid];
}

/* AUX */
process_id_t process_get_free_table_slot( void ) 
{
  process_id_t pid = 1;
  process_id_t first_dead_pid = -1;
    
  /*
  interrupt_status_t intr_status;
  intr_status = _interrupt_disable( );
  spinlock_acquire( &pt_slock );   
  */

  interrupt_status_t intr_status;
  intr_status = _interrupt_disable( );
  spinlock_acquire( &pt_slock );   
  /*==========LOCKED==========*/
  /* search table for a FREE slot. keep track of DYING slot.*/
  while( process_table[pid].state != PROCESS_FREE && pid < PROCESS_MAX_PROCESSES ) { 
    if( first_dead_pid == -1 && process_table[pid].state == PROCESS_DEAD ) { 
      first_dead_pid = pid;
    }
    pid++; 
  } 
  /* no free slot found. try dying */
  if( process_table[pid].state != PROCESS_FREE ) { pid = first_dead_pid; } 
  if( pid != -1 ) { process_table[pid].state = PROCESS_NONREADY; }
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_set_state( intr_status );

  return pid;
}

/* this function is used to wrap thread function call. 
 * could be usefull... 
 */
void process_wrapper( process_id_t pid )
{
  process_start( pid );
  DEBUG( "debug_processes", "process_wrapper process_start returned\n" );
}
/** @} */
