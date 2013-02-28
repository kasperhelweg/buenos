/*
 * Main startup routines for BUENOS
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
 * $Id: main.c,v 1.25 2006/01/13 05:44:34 jaatroko Exp $
 *
 */

#include "drivers/bootargs.h"
#include "drivers/device.h"
#include "drivers/gcd.h"
#include "drivers/metadev.h"
#include "drivers/polltty.h"
#include "drivers/yams.h"
#include "fs/vfs.h"
#include "kernel/lock_cond.h"
#include "kernel/assert.h"
#include "kernel/config.h"
#include "kernel/halt.h"
#include "kernel/idle.h"
#include "kernel/interrupt.h"
#include "kernel/kmalloc.h"
#include "kernel/panic.h"
#include "kernel/scheduler.h"
#include "kernel/synch.h"
#include "kernel/thread.h"
#include "lib/debug.h"
#include "lib/libc.h"
#include "net/network.h"
#include "proc/process.h"
#include "vm/vm.h"


#define DTHREADS 29
#define VECLENGTH DTHREADS * 5
typedef struct DOTDATA 
 {
   int      a[VECLENGTH];
   int      b[VECLENGTH];
   int     sum; 
   int     veclen; 
 } DOTDATA;

static DOTDATA* dotdata;
static int threadcount;

static lock_t lock;

static int* read;

void thread_a( uint32_t arg)
{
  int a;

  if( arg == 0 ){
    lock_acquire( &lock );
    a = *read;
    thread_yield( );
    lock_release( &lock );
  } else {
    a = *read;
    thread_yield( );
  }

  if( a == *read){
    kprintf("GREAT!\n");
  } else {
    kprintf("FAIL!\n");
  }
  thread_finish( );
}

void thread_b( uint32_t arg)
{
  if( arg == 0 ){
    lock_acquire( &lock );
    *read = 1;
    lock_release( &lock );
  } else {
    *read = 1;
  }
  kprintf("READ: %d\n", *read);
  thread_finish( );
} 

void dot_thread( uint32_t arg)
{
  arg = arg;
  int i;
  
  int index = thread_get_current_thread( ) - 3;
  int sum = 0;
  
  for(i=0; i<5; i++){
    sum += (dotdata->a)[index + 1] * (dotdata->b)[index + 1];
  }
  
  lock_acquire( &lock );
  kprintf("Thread after lock acquire: %d with sum: %d\n", thread_get_current_thread( ), sum);
  (dotdata->sum) += sum;
  lock_release( &lock );
 
  threadcount--;
  thread_finish();
}

/**
 * Fallback function for system startup. This function is executed
 * if the initial startup program (shell or other userland process given
 * with initprog boot argument) is not started or specified.
 *
 * This is a good place for some kernel test code!
 *
 */

void init_startup_fallback(void) {

  DEBUG("debuginit", "In init_startup_fallback\n");

  /* Run console test if "testconsole" was given as boot argument. */
  if (bootargs_get("testconsole") != NULL) {
    device_t *dev;
    gcd_t *gcd;
    char buffer[64];
    char buffer2[64];
    int len;

    DEBUG("debuginit", "In console test\n");

    /* Find system console (first tty) */
    dev = device_get(YAMS_TYPECODE_TTY, 0);
    KERNEL_ASSERT(dev != NULL);

    gcd = (gcd_t *)dev->generic_device;
    KERNEL_ASSERT(gcd != NULL);

    len = snprintf(buffer, 63, "Hello user! Press any key.\n");
    gcd->write(gcd, buffer, len);

    len = gcd->read(gcd, buffer2, 63);
    KERNEL_ASSERT(len >= 0);
    buffer2[len] = '\0';

    len = snprintf(buffer, 63, "You said: '%s'\n", buffer2);
    gcd->write(gcd, buffer, len);

    DEBUG("debuginit", "Console test done, %d bytes written\n", len);
  }

  if (bootargs_get("mutex_dot") != NULL) {
    int i;
    TID_t t[DTHREADS];
    
    threadcount = 0;
    if ( lock_reset(&lock) != 0 ){
      KERNEL_PANIC("FUCK LOCK.\n");
    }

    /* initialize data */
    dotdata->sum = 0;
    dotdata->veclen = VECLENGTH;
    
    for(i=0; i<VECLENGTH; i++){
      (dotdata->a)[i] = i;
      (dotdata->b)[i] = i;
    }
    
    /* create threads */
    for(i=0; i<DTHREADS; i++){
      t[i] = thread_create(&dot_thread, 0 );
      threadcount++;
    }
    /* launch threads */
    for(i=0; i<DTHREADS; i++){
      thread_run( t[i]  );
    }
    /* wait fot threads to finish */
    while( threadcount != 0 ){
      thread_switch( );
    }
    kprintf("Dotproduct: %d\n", dotdata->sum);
  }

  if (bootargs_get("mutex_lock") != NULL) {
    TID_t a;
    TID_t b;
    
    *read = 0;
    lock_reset(&lock);

    a = thread_create(&thread_a, 1 );
    b = thread_create(&thread_b, 1 );

    thread_run( a );
    thread_run( b );
    

  }
  
  /* Nothing else to do, so we shut the system down. */
  kprintf("Startup fallback code ends.\n");
  halt_kernel();
}

/**
 * Initialize the system. This function is called from the first
 * system thread fired up by the boot code below.
 *
 * @param arg Dummy argument, unused.
 */

void init_startup_thread(uint32_t arg)
{
  /* Threads have arguments for functions they run, we don't
     need any. Silence the compiler warning by using the argument. */
  arg = arg;
  
  kprintf("Mounting filesystems\n");
  vfs_mount_all();
  
  kprintf("Initializing networking\n");
  network_init();
  
  if(bootargs_get("initprog") == NULL) {
    kprintf("No initial program (initprog), dropping to fallback\n");
    init_startup_fallback();
  }
  
  kprintf("Starting initial program '%s'\n", bootargs_get("initprog"));
  
  /* process_start(bootargs_get("initprog")); */
  process_init_process( bootargs_get( "initprog" ) );
    
  /* The current process_start() should never return. */
  KERNEL_PANIC("Run out of initprog.\n");
}

/* Whether other processors than 0 may continue in SMP mode.
   CPU0 runs the actual init() below, other CPUs loop and wait
   this variable to be set before they will enter context switch
   and scheduler to get a thread to run. */
int kernel_bootstrap_finished = 0;


/**
 * Initialize the system. This function is called by CPU0 just
 * after the kernel code is entered first time after boot.
 *
 * The system is still in bootup-mode and can't run threads or multiple
 * CPUs.
 *
 */

void init(void)
{
  TID_t startup_thread;
  int numcpus;

  /* Initialize polling TTY driver for kprintf() usage. */
  polltty_init();

  kwrite("BUENOS is a University Educational Nutshell Operating System\n");
  kwrite("==========================================================\n");
  kwrite("\n");

  kwrite("Copyright (C) 2003-2006  Juha Aatrokoski, Timo Lilja,\n");
  kwrite("  Leena Salmela, Teemu Takanen, Aleksi Virtanen\n");
  kwrite("See the file COPYING for licensing details.\n");
  kwrite("\n");

  kwrite("Initializing memory allocation system\n");
  kmalloc_init();

  kwrite("Reading boot arguments\n");
  bootargs_init();

  /* Seed the random number generator. */
  if (bootargs_get("randomseed") == NULL) {
    _set_rand_seed(0);
  } else {
    int seed = atoi(bootargs_get("randomseed"));
    kprintf("Seeding pseudorandom number generator with %i\n", seed);
    _set_rand_seed(seed);
  }

  /* allocate memory for test datastructures */
  if (bootargs_get("mutex_dot") != NULL) {
  dotdata = (DOTDATA*)kmalloc( sizeof( DOTDATA ) );
  }
  if (bootargs_get("mutex_lock") != NULL) {
  read = (int*)kmalloc( sizeof( int ) );
  }

  numcpus = cpustatus_count();
  kprintf("Detected %i CPUs\n", numcpus);
  KERNEL_ASSERT(numcpus <= CONFIG_MAX_CPUS);

  kwrite("Initializing interrupt handling\n");
  interrupt_init(numcpus);

  kwrite("Initializing threading system\n");
  thread_table_init();

  kwrite("Initializing process table\n");
  process_init( );

  kwrite("Initializing sleep queue\n");
  sleepq_init();

  kwrite("Initializing semaphores\n");
  semaphore_init();

  kwrite("Initializing device drivers\n");
  device_init();

  kprintf("Initializing virtual filesystem\n");
  vfs_init();

  kwrite("Initializing scheduler\n");
  scheduler_init();

  kwrite("Initializing virtual memory\n");
  vm_init();
    
  kprintf("Creating initialization thread\n");
  
  startup_thread = thread_create(&init_startup_thread, 0);
  thread_run( startup_thread );

  kprintf("Starting threading system and SMP\n");

  /* Let other CPUs run */
  kernel_bootstrap_finished = 1;

  _interrupt_clear_bootstrap();
  _interrupt_enable();

  /* Enter context switch, scheduler will be run automatically,
     since thread_switch() behaviour is identical to timer tick
     (thread timeslice is over). */
  thread_switch();

  /* We should never get here */
  KERNEL_PANIC("Threading system startup failed.");
}
