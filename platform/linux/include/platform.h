#include <stdint.h>
#define _GNU_SOURCE
#include <pthread.h>

/* OS primitives */
enum
{
	False,
	True
};

#define nil ((void * )0)

typedef pthread_mutex_t heaplib_lock_t;
typedef volatile uint8_t * vbaddr_t;
typedef volatile size_t * vaddr_t;
typedef pthread_t task_t;

/* Locking primitives */
#define heaplib_lock_init(x) pthread_mutex_init((x));
#define heaplib_unlock(x) pthread_mutex_unlock((x));
#define heaplib_lock(x) pthread_mutex_lock((x));
#define heaplib_trylock(x) pthread_mutex_trylock((x));

/* Scheduling */
#define SCHEDULE_TASK(x) /* Nothing to do on Linux */
#define YIELD() platform_yield();

/* How many regions do we support? In the future, this will be dynamic */
#define NREGIONS 4

extern void platform_yield(void);

