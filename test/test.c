#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "heaplib/heaplib.h"

#define MEMSZ (32 * 1024)

int
main(int argc, char * argv[])
{
	caddr_t x[4];
	caddr_t f;
	int i;
	int r;
	int t;
	int sz;

	srandom(time(nil) ^ getpid());

	printf("--- init %d ---\n", getpid());
	mem_init(calloc(1, MEMSZ), MEMSZ);
	printf("\n");

	memset(&x[0], 0, sizeof x);

	i = 0;
	while(True)
	{
		if(!x[i])
		{
			printf("--- alloc ---\n");
			sz = random() % MEMSZ;
			printf("requesting alloc sz=%d\n", sz);
			if(!mem_calloc(&x[i], 1, sz, 0))
				break;
			printf("allocated: p=%p size=%d\n", x[i], sz);
			printf("\n");
		}

		i++;
		if(i == 4)
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
					mem_free(&f);
				}
			}

			mem_walk();
			printf("\n");
			i = 0;
		}
	}

	printf("\n--- END ---\n");
	printf("got OOM?\n");

	mem_walk();
}

