#include "kernel/lock_cond.h"
#include "lib/debug.h"
#include "kernel/thread.h"
#include "kernel/assert.h"
#include "kernel/interrupt.h"
#include "kernel/config.h"
#include "kernel/sleepq.h"
#include "drivers/metadev.h"

/** @name Mutex locks
 *
 * The sleep queue is the mechanism which allows threads to go to
 * sleep while waiting on a resource to become available and be woken
 * once said resource does become available. A thread going to sleep
 * waiting for a resource (usually a memory address) is placed in a
 * hash table, from which it can be quickly found and awakened once
 * the resource becomes available.
 *
 * The resources are referenced by memory address. The address is used
 * only as a key, it is never referenced by the sleep queue mechanism.
 *
 * @{
 */

int lock_reset( lock_t* lock )
{
  if( lock != NULL ) {
    spinlock_reset( &(lock->slock) );
    lock->state = LOCK_FREE; 
    return 0;
  } else {
    return -1;
  }
}

void lock_acquire( lock_t* lock )
{
  interrupt_status_t intr_status;

  intr_status = _interrupt_disable( );
  spinlock_acquire( &(lock->slock) );
  /*==========LOCKED==========*/
  /* add current thread to sleep queue. sleep on the lock */
  while( lock->state != LOCK_FREE ){
    sleepq_add( lock );
    spinlock_release( &(lock->slock) );
    thread_switch();
    spinlock_acquire( &(lock->slock) );
  }
  /* acquire the lock on wakeup. */
  lock->state = LOCK_LOCKED; 
  lock->count++;
  /*==========LOCKED==========*/
  spinlock_release( &(lock->slock) );
  _interrupt_set_state( intr_status );
}

void lock_release( lock_t* lock )
{
  interrupt_status_t intr_status;

  intr_status = _interrupt_disable( );
  spinlock_acquire( &(lock->slock) );
  /*==========LOCKED==========*/
  /* free the lock */
  lock->state = LOCK_FREE; 
  /* wake first thread waiting */
  sleepq_wake( lock );
  /*==========LOCKED==========*/
  spinlock_release( &(lock->slock) );
  _interrupt_set_state( intr_status );
}

/* condition variables */
void condition_init( cond_t* cond )
{
  cond->state = 666;
}

void condition_wait( cond_t* cond, lock_t* lock )
{
  interrupt_status_t intr_status;

  intr_status = _interrupt_disable( );
  spinlock_acquire( &(lock->slock) );
  /*==========LOCKED==========*/
  /* sleep on cindition */
  sleepq_add( cond ); 
  /* the lock is realesed without a call to lock_release
   * since this would require the release of the spinlock
   * thus another thread might steal the session
  */
  lock->state = LOCK_FREE; 
  sleepq_wake( lock );
  /*==========LOCKED==========*/
  spinlock_release( &(lock->slock) );  
  _interrupt_set_state( intr_status );
  
  thread_switch();
  lock_acquire( lock );
}

void condition_signal( cond_t* cond, lock_t* lock )
{
  /* don't need to do anything with the lock right now */
  lock = lock;
  /* wake thread sleeping on cond */
  sleepq_wake( cond );
}

void condition_broadcast( cond_t* cond, lock_t* lock )
{
  /* don't need to do anything with the lock right now */
  lock = lock;
  /* wake all thread's sleeping on cond */
  sleepq_wake_all( cond );
}


