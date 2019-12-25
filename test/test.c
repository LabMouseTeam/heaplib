#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "heaplib/heaplib.h"

#define MEMSZ (32 * 1024 )
#define ALLOCSZ (1 * 1024)

int
main(int argc, char * argv[])
{
	vaddr_t x[32];
	vaddr_t f;
	int i;
	int r;
	int t;
	int sz;

	srandom(time(nil) ^ getpid());

	printf("--- init %d ---\n", getpid());
	heaplib_region_add(calloc(1, MEMSZ), MEMSZ, heaplib_flags_internal);
	printf("\n");

	memset(&x[0], 0, sizeof x);

	i = 0;
	while(True)
	{
		if(!x[i])
		{
			printf("--- alloc ---\n");
			sz = random() % ALLOCSZ;
			if(sz == 0)
				sz = 1;
			printf("requesting alloc sz=%d\n", sz);
			if(heaplib_calloc(&x[i], 1, sz, 0) != heaplib_error_none)
				break;
			printf("allocated: p=%p size=%d\n", x[i], sz);
			printf("\n");
			heaplib_walk();
		}

		i++;
		if(i == nelem(x))
		{
			printf("--- free ---\n");
			printf("wiping %d allocs\n", t);
			t = random() % 4;

			for(i = 0; i < t; i++)
			{
				r = random() % 4;
				if(x[r])
				{
					printf("freeing p=%p\n", x[r]);
					f = x[r];
					x[r] = nil;
					heaplib_free(&f, 0);
				}
			}

			heaplib_walk();
			printf("\n");
			i = 0;
		}
	}

	printf("\n--- END ---\n");
	printf("got OOM?\n");

	heaplib_walk();
}

