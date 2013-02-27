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
  /* numcpus = cpustatus_count(); */
  lock->state = LOCK_FREE;
  lock->owner = 0;
  lock->count = 0;
  
  return lock->state;
}

void lock_acquire( lock_t* lock )
{
  switch( lock-> state ) {
  case LOCK_FREE:
    lock = lock;               
    break;
  case LOCK_LOCKED:
    /* dummy */
    lock = lock;
    break;
  default:
    lock = lock;
  }
}

void lock_release( lock_t* lock )
{
  lock = lock;
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
