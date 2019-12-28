#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "heaplib/heaplib.h"

#define MEMSZ (32 * 1024 )
#define ALLOCSZ ((128 * 10))

struct
test_unit_t
{
	vaddr_t a;
	uint8_t c;
	size_t sz;
};

typedef struct test_unit_t test_unit_t;

static boolean_t interrupted = False;

boolean_t
validate(test_unit_t * x)
{
	vbaddr_t b;
	int i;

	b = (vbaddr_t)x->a;
	for(i = 0; i < x->sz; i++)
	{
		if(b[i] != x->c)
		{
			fprintf(stdout, "validation failed on x=%p offset=%d c=%x\n", x, i, x->c);
			fflush(stdout);
			return False;
		}
	}

	return True;
}

static void
sighandler(int x)
{
	interrupted = True;
}

int
main(int argc, char * argv[])
{
	test_unit_t x[32];
	uint8_t * region;
	vaddr_t f;
	int i;
	int r;
	int t;
	int sz;
	int cap;
	int allocs, frees;

	signal(SIGINT, sighandler);

	srandom(time(nil) ^ getpid());

	printf("--- init %d ---\n", getpid());
	fflush(stdout);

	region = (void * )calloc(1, MEMSZ);
	heaplib_region_add((void*)region, MEMSZ /*/ 2*/, heaplib_flags_internal /*| heaplib_flags_smallreq*/);
	// heaplib_region_add((void*)(region + (MEMSZ / 2)), MEMSZ / 2, heaplib_flags_internal /*| heaplib_flags_largereq*/);
	printf("\n");
	fflush(stdout);

	memset(&x[0], 0, sizeof x);

	allocs = 0; frees = 0;
	i = 0;
	cap = random() % nelem(x);
	if(!cap)
		cap = 1;

	while(!interrupted)
	{
		if(!x[i].a)
		{
			printf("--- alloc ---\n");
			fflush(stdout);

			sz = random() % ALLOCSZ;
			if(sz == 0)
				sz = 1;

			printf("requesting alloc sz=%d\n", sz);
			fflush(stdout);

			x[i].sz = sz;
			if(heaplib_calloc(&x[i].a, 1, x[i].sz, 0) != heaplib_error_none)
				break;
			x[i].c = random() % 254;
			if(!x[i].c)
				x[i].c = 1;

			printf("allocated: p=%p size=%ld c=%x\n\n", x[i].a, x[i].sz, x[i].c);
			fflush(stdout);
			memset((void * )x[i].a, x[i].c, x[i].sz);

			allocs++;

			heaplib_walk();
		}

		i++;
		if(i == cap)
		{
			printf("--- free ---\n");
			fflush(stdout);

			t = random() % cap;
			printf("wiping %d allocs\n", t);
			fflush(stdout);

			for(i = 0; i < t; i++)
			{
				r = random() % cap;
				if(x[r].a)
				{
					if(!validate(&x[r]))
					{
						fprintf(stdout, "exiting due to validation failure\n");
						fflush(stdout);
						return 1;
					}

					printf("freeing p=%p\n", x[r].a);
					fflush(stdout);

					f = x[r].a;
					x[r].a = nil;
					x[r].sz = 0;
					heaplib_free(&f, 0);
					frees++;
				}
			}

			heaplib_walk();
			printf("\n");
			fflush(stdout);
			i = 0;

			cap = random() % nelem(x);
			if(!cap)
				cap = 1;
		}

	}

	printf("\n--- %s ---\n", interrupted ? "interrupted" : "END");
	fflush(stdout);
	printf("got OOM? allocs=%d frees=%d\n", allocs, frees);
	fflush(stdout);

	heaplib_walk();
}

