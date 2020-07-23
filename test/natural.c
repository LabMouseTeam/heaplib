#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "heaplib/heaplib.h"

#define NTHREADS 16

#define BIGMEMSZ (512 * 1024 )
#define MEMSZ (32 * 1024 )
#define ALLOCSZ ((128 * 14))

struct
test_unit_t
{
	vaddr_t a;
	uint8_t c;
	size_t sz;
};

typedef struct test_unit_t test_unit_t;

static pthread_mutex_t stats;
int allocs;
int frees;

static pthread_mutex_t lock;
static boolean_t oom = False;
static boolean_t interrupted = False;

static void * run(void * );

boolean_t
validate(test_unit_t * x)
{
	vbaddr_t b;
	int i;

	b = (vbaddr_t)x->a;
	for(i = 0; i < (int)x->sz; i++)
	{
		if(b[i] != x->c)
		{
			PRINTF("%ld: error: validation failed on x=%p offset=%d c=%x\n", pthread_self(), x, i, x->c);
			return False;
		}
	}

	return True;
}

static void
sighandler(int _x)
{
	USED(_x);
	interrupted = True;
}

int
main(void)
{
	pthread_t threads[NTHREADS];
	uint8_t * region;
	boolean_t b;
	int i;

	PRINTF("main!\n");

	PRINTF("SIZEOF node=%d foot=%d\n", sizeof(heaplib_node_t), sizeof(heaplib_footer_t));

	signal(SIGINT, sighandler);
	srandom(time(nil) ^ getpid());

	heaplib_init();

	region = (void * )calloc(1, MEMSZ);
	heaplib_region_add((void*)region, MEMSZ /*/ 2*/, heaplib_flags_internal /*| heaplib_flags_smallreq*/);
	// heaplib_region_add((void*)(region + (MEMSZ / 2)), MEMSZ / 2, heaplib_flags_internal /*| heaplib_flags_largereq*/);

	/* XXX add second region for testing with no flags */
	region = (void * )calloc(1, BIGMEMSZ);
	heaplib_region_add((void*)region, BIGMEMSZ /*/ 2*/, 0);

	pthread_mutex_init(&lock, nil);
	pthread_mutex_init(&stats, nil);

	for(i = 0; i < NTHREADS; i++)
	{
		pthread_create(&threads[i], nil, run, nil);
	}

	while(True)
	{
		pthread_mutex_lock(&lock);
		b = oom;
		pthread_mutex_unlock(&lock);
		if(b)
		{
			PRINTF("oom!\n");
			break;
		}

		pthread_mutex_lock(&lock);
		b = interrupted;
		pthread_mutex_unlock(&lock);
		if(b)
		{
			PRINTF("interrupted; main exiting\n");
			break;
		}

		usleep(100);
	}

	for(i = 0; i < NTHREADS; i++)
	{
		PRINTF("main: joining thread[%d]=%ld\n", i, threads[i]);
		pthread_join(threads[i], nil);
	}

	PRINTF("stats allocs=%d frees=%d\n", allocs, frees);
	//heaplib_walk();

	return 0;
}

static void *
run(void * _x)
{
	test_unit_t x[32];
	vaddr_t f;
	int i;
	int r;
	int t;
	int sz;
	int cap;
	boolean_t b;
	int natural;
	heaplib_flags_t flags;

	USED(_x);

	PRINTF("--- init thread=%ld ---\n", pthread_self());

	memset(&x[0], 0, sizeof x);

	allocs = 0; frees = 0;
	i = 0;
	cap = random() % nelem(x);
	if(!cap)
		cap = 1;

	while(True)
	{
		pthread_mutex_lock(&lock);
		b = oom;
		pthread_mutex_unlock(&lock);
		if(b)
		{
			PRINTF("%ld: oom detected in alternative thread...\n", pthread_self());
			break;
		}

		pthread_mutex_lock(&lock);
		b = interrupted;
		pthread_mutex_unlock(&lock);
		if(b)
		{
			PRINTF("%ld: interrupt detected... bailing..\n", pthread_self());
			break;
		}

		if(!x[i].a)
		{
			PRINTF("%ld: --- alloc ---\n", pthread_self());

			/* 10% chance at every allocation that we natural */
			natural = random() % 10;
			if(natural == 1)
			{
				// test up to 4096
				natural = (random() % 9) + 3;
				sz = 1 << natural;
				PRINTF("%ld: attempting NATURAL sz=%d\n", pthread_self(), sz);
				flags = heaplib_flags_wait | heaplib_flags_natural;
			}
			else
			{
				natural = 0;
				sz = random() % ALLOCSZ;
				if(sz == 0)
					sz = 1;
				flags = heaplib_flags_wait;
			}

			PRINTF("%ld: requesting alloc sz=%d\n", pthread_self(), sz);

			x[i].sz = sz;
			if(heaplib_calloc(&x[i].a, 1, x[i].sz, flags) != heaplib_error_none)
			{
				PRINTF("OOM in thread: %ld\n", pthread_self());
				pthread_mutex_lock(&lock);
				oom = True;
				pthread_mutex_unlock(&lock);
				break;
			}

			if(natural && ((size_t)x[i].a & (sz - 1)))
			{
				PRINTF("error: address %p is unnatural sz=%ld\n", x[i].a, sz);
			}

			x[i].c = random() % 254;
			if(!x[i].c)
				x[i].c = 1;

			PRINTF("%ld: allocated: p=%p size=%ld c=%x\n\n", pthread_self(), x[i].a, x[i].sz, x[i].c);
			memset((void * )x[i].a, x[i].c, x[i].sz);

			pthread_mutex_lock(&stats);
			allocs++;
			pthread_mutex_unlock(&stats);

			//heaplib_walk();
		}

		i++;
		if(i == cap)
		{
			PRINTF("%ld: --- free ---\n", pthread_self());

			t = random() % cap;
			PRINTF("%ld: wiping %d allocs\n", pthread_self(), t);

			for(i = 0; i < t; i++)
			{
				r = random() % cap;
				if(x[r].a)
				{
					if(!validate(&x[r]))
					{
						PRINTF("%ld: thread EXIT due to validation failure\n", pthread_self());
						return nil;
					}

					PRINTF("%ld: freeing p=%p\n", pthread_self(), x[r].a);

					f = x[r].a;
					x[r].a = nil;
					x[r].sz = 0;
					heaplib_free(&f, heaplib_flags_wait);

					pthread_mutex_lock(&stats);
					frees++;
					pthread_mutex_unlock(&stats);
				}
			}

			//heaplib_walk();
			PRINTF("\n");
			i = 0;

			cap = random() % nelem(x);
			if(!cap)
				cap = 1;
		}

	}

	PRINTF("\n--- EXIT thread=%ld %s ---\n", pthread_self(), interrupted ? "interrupted" : "OOM");

	return nil;
}

