/** @name Mutex locks
 * @{
 */
#include "kernel/spinlock.h"

typedef enum {
  LOCK_FREE,
  LOCK_LOCKED
} lock_state_t;

typedef struct lock_t {
  lock_state_t state;
  spinlock_t slock;
  int count;
} lock_t;

typedef int cond_state_t;
typedef struct cond_t {
  cond_state_t state;
} cond_t;

/*
 * Initialize a lock
 * 
 */
int lock_reset( lock_t* lock );
/*
 * This procedure will try to acquire the lock
 * If it's not succesfull, the calling thread is put in the sleep queue.
 */
void lock_acquire( lock_t* lock );
void lock_release( lock_t* lock );
int lock_try_lock( lock_t* lock );

/* condition variables */
void condition_init( cond_t* cond );
void condition_wait( cond_t* cond, lock_t* lock );
void condition_signal( cond_t* cond, lock_t* lock );
void condition_broadcast( cond_t* cond, lock_t* lock );
