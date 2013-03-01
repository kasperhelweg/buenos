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

static spinlock_t lock_cond_slock;

int lock_reset( lock_t* lock )
{
  /* if on mult.proc. machine, fallback to spinlock numcpus = cpustatus_count(); */
  if( lock != NULL && lock->initialized != 1 ) {
    spinlock_reset( &lock_cond_slock );
    lock->state = LOCK_FREE; lock->owner = LOCK_NOT_OWNED; lock->count = 0; lock->initialized = 1;
    return LOCK_FREE;
  } else {
    return LOCK_RESET_FAILED;
  }
}

void lock_acquire( lock_t* lock )
{
  interrupt_status_t intr_status;

  intr_status = _interrupt_disable( );
  spinlock_acquire( &lock_cond_slock );
  /*==========LOCKED==========*/
  switch( lock->state ) {
  case LOCK_FREE:
    lock->state = LOCK_LOCKED; lock->owner = (int)thread_get_current_thread( ); lock->count++;
    spinlock_release( &lock_cond_slock );
    break;
  case LOCK_LOCKED:
    while( lock->state != LOCK_FREE ){
      sleepq_add( lock );
      spinlock_release( &lock_cond_slock );
      thread_switch();
      spinlock_acquire( &lock_cond_slock );
    }
    lock->state = LOCK_LOCKED; lock->owner = (int)thread_get_current_thread( ); lock->count++;
    spinlock_release( &lock_cond_slock );
    break;
  default:
    spinlock_release( &lock_cond_slock );
  }
  /*==========LOCKED==========*/
  _interrupt_set_state( intr_status );
}

void lock_release( lock_t* lock )
{
  interrupt_status_t intr_status;

  intr_status = _interrupt_disable( );
  spinlock_acquire( &lock_cond_slock );
  /*==========LOCKED==========*/
  lock->state = LOCK_FREE; lock->owner = LOCK_NOT_OWNED; 
  sleepq_wake( lock );
  /*==========LOCKED==========*/
  spinlock_release( &lock_cond_slock );
  _interrupt_set_state( intr_status );
}

lock_state_t lock_try_lock( lock_t* lock )
{
  return lock->state;
}

/* condition variables */
void condition_init( cond_t* cond )
{
  cond = cond;
}

void condition_wait( cond_t* cond, lock_t* lock )
{
  lock = lock;
  cond = cond;
}

void condition_signal( cond_t* cond, lock_t* lock )
{
  lock = lock;
  cond = cond;
}

void condition_broadcast( cond_t* cond, lock_t* lock )
{
  lock = lock;
  cond = cond;
}
