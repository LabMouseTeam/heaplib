/**
 * \file platform/harvest/include/platform/platform.h
 *
 * \brief Platform fundamentals for Lab Mouse heaplib.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date July 25, 2020
 */
#ifdef INTERNAL
# include "arch/stdint.h"
# include "subarch/sched.h"
# include "stdlib.h"
# include "mutex.h"
# include "task.h"
#endif

typedef harvest_task_t * task_t;
typedef harvest_mutex_t heaplib_lock_t;

/* Locking primitives */
#define heaplib_lock_init(x) 	mutex_init((x));
#define heaplib_lock_lock(x) 	mutex_lock((x));
#define heaplib_lock_unlock(x) 	mutex_unlock((x));
__attribute__((always_inline)) __inline__ int
heaplib_lock_trylock(heaplib_lock_t * x) {
	heaplib_lock_lock(x);
	return True;
}

/* Debugging and printing */
#ifdef DEBUG
# define PRINTF( ... ) printf(__VA_ARGS__)
#else
# define PRINTF( ... )
#endif

/* Scheduling */
#define SCHEDULE_TASK(x) /* Nothing to do on Linux */
#define YIELD() yield();
#define GET_PLATFORM_TASKID() (task_t)nil

/* How many regions do we support? In the future, this will be dynamic */
#define NREGIONS 1

