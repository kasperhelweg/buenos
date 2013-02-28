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

#define LOCK_RESET_FAILED -1
#define LOCK_NOT_OWNED -1

typedef enum {
  LOCK_FREE,
  LOCK_LOCKED
} lock_state_t;

typedef struct lock_t {
  lock_state_t state;
  int owner;
  unsigned int count;
} lock_t;

/* dummy. remove */
typedef int cond_t;

int lock_reset( lock_t* lock );
void lock_acquire( lock_t* lock );
void lock_release( lock_t* lock );
lock_state_t lock_try_lock( lock_t* lock );

/* condition variables */
void condition_init( cond_t* cond );
void condition_wait( cond_t* cond, lock_t* lock );
void condition_signal( cond_t* cond, lock_t* lock );
void condition_broadcast( cond_t* cond, lock_t* lock );
