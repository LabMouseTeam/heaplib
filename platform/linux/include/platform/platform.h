#include <stdint.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <pthread.h>

/* OS primitives */
enum
{
	False,
	True
};

#define nil ((void * )0)

#define nelem(x) (sizeof(x)/sizeof((x)[0]))

typedef int boolean_t;
typedef pthread_t task_t;
typedef volatile size_t * vaddr_t;
typedef volatile uint8_t * vbaddr_t;
typedef pthread_mutex_t heaplib_lock_t;

/* Locking primitives */
#define heaplib_lock_init(x) 	pthread_mutex_init((x), nil);
#define heaplib_lock_lock(x) 	pthread_mutex_lock((x));
#define heaplib_lock_unlock(x) 	pthread_mutex_unlock((x));
__attribute__((always_inline)) __inline__ int
heaplib_lock_trylock(heaplib_lock_t * x) {
	return pthread_mutex_lock(x);
}

/* Debugging and printing */
#ifdef DEBUG
# define PRINTF( ... ) thread_printf(__VA_ARGS__)
#else
# define PRINTF( ... )
#endif

/* Scheduling */
#define SCHEDULE_TASK(x) /* Nothing to do on Linux */
#define YIELD() platform_yield();
#define GET_PLATFORM_TASKID() (task_t)nil

/* How many regions do we support? In the future, this will be dynamic */
#define NREGIONS 4

extern void platform_yield(void);
extern void thread_printf(const char *, ... );

