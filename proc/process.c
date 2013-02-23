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
  my_entry->process_id = pid; 

  /* set executable name */
  executable = process_table[pid].name;
  
  /* ====== DEBUG START ====== */
  DEBUG( "debug_processes", "process_start() says ~ executable name: %s\n", executable );
  DEBUG( "debug_processes", "process_start() says ~ tid: %d\n", thread_get_current_thread( ) );
  DEBUG( "debug_processes", "process_start() says ~ pid: %d\n", pid );
  /* ====== DEBUG END ====== */

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

  _interrupt_disable( );
  spinlock_acquire( &pt_slock );  
  /*==========LOCKED==========*/
  process_table[pid].state = PROCESS_RUNNING;
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_enable( );

  thread_goto_userland(&user_context);

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
    process_table[pid].pid = pid;
    process_table[pid].state = PROCESS_FREE;
    process_table[pid].parent = NULL;
  }
}

void process_init_process( const char* executable ) 
{  
  process_control_block_t* init_process;
  
  _interrupt_disable( );
  spinlock_acquire( &pt_slock );  
  /*==========LOCKED==========*/
  init_process = &process_table[0];
  /* this can only be called from startup_thread */
  if( init_process->state == PROCESS_FREE && thread_get_current_thread( ) == 1 ) { 
    /* set entries in PCB. */
    init_process->tid = 1;     
    init_process->name = executable;
    init_process->state = PROCESS_READY;
    /*==========LOCKED==========*/
    spinlock_release( &pt_slock );  
    _interrupt_enable( );
  } else { 
    KERNEL_PANIC("Create initial process failed!");
  }
  process_start( 0 );
}

process_id_t process_spawn( const char* executable ) 
{  
  process_control_block_t* process;
  process_id_t child_pid;
  TID_t child_tid;
    
  /* get free slot in process_table */
  child_pid = process_get_free_table_slot( );
  if( child_pid != -1 ){
    /* create a new thread and run it if process creation was succesfull */
    process = &process_table[child_pid];
    child_tid = thread_create((void (*)(uint32_t))&process_wrapper, (uint32_t)child_pid);

    _interrupt_disable( );
    spinlock_acquire( &pt_slock );  
    /*==========LOCKED==========*/
    process->tid = child_tid;
    process->name = executable;
    process->state = PROCESS_READY;
    process->parent = &process_table[process_get_current_process()];
    /*==========LOCKED==========*/
    spinlock_release( &pt_slock );  
    _interrupt_enable( );

    thread_run( child_tid );
  } else {
    /* do stuff. no more processes alloud*/
  }
  return child_pid; 
}

/* Stop the process and the thread it runs in. Sets the return value as well */
void process_finish( int retval ) 
{
  /* ====== DEBUG START ====== */
  /* DEBUG( "debug_processes", "process_finish() says ~ current_process: %s\n", process_table[process_get_current_process( )].name ); */
  /* ====== DEBUG END ====== */
  
  thread_table_t* current_thread_entry;
  process_control_block_t* current_process;
  
  current_thread_entry = thread_get_current_thread_entry( );
  current_process = &process_table[process_get_current_process( )];

  _interrupt_disable( );
  spinlock_acquire( &pt_slock );  
  /*==========LOCKED==========*/
  current_process->state = PROCESS_DYING;
  current_process->return_code = retval;
  sleepq_wake( current_process );
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_enable( );

  /* kill thread */
  vm_destroy_pagetable( current_thread_entry->pagetable );
  current_thread_entry->pagetable = NULL;
  thread_finish( );
}

int process_join( process_id_t pid ) 
{
  int retval;
  process_control_block_t* process;
  process = &process_table[pid];
  
  _interrupt_disable( );
  spinlock_acquire( &pt_slock );  
  /*==========LOCKED==========*/
  while( process->state != PROCESS_DYING ){
    sleepq_add( process );
    spinlock_release( &pt_slock );
    thread_switch( );
    spinlock_acquire( &pt_slock );  
  } 
  retval = process->return_code;
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_enable( );
  
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
  process_id_t pid;
  process_id_t dying_pid;
  pid = 1;
  dying_pid = -1;
 
  _interrupt_disable( );
  spinlock_acquire( &pt_slock );  
  /*==========LOCKED==========*/
  /* search table for a FREE slot. keep track of DYING slot.*/
  while( process_table[pid].state != PROCESS_FREE && pid < PROCESS_MAX_PROCESSES ) { 
    if( process_table[pid].state == PROCESS_DYING && dying_pid == -1 ) { 
      dying_pid = pid;
    }
    pid++; 
  } 
  /* no free slot found. try dying */
  if( process_table[pid].state != PROCESS_FREE ) { pid = dying_pid; } 
  /*==========LOCKED==========*/
  spinlock_release( &pt_slock );  
  _interrupt_enable( );
   
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
