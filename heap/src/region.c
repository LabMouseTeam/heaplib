#include "heaplib/heaplib.h"
#include <stdio.h>

static heaplib_region_t regions[NREGIONS];

static heaplib_lock_t heaplib_region_lock;

static void __region_walk(heaplib_region_t * );
static void __heaplib_node_init(heaplib_region_t *, heaplib_node_t * );
static heaplib_error_t __region_test_and_lock(heaplib_region_t *, heaplib_flags_t);

void
heaplib_walk(void)
{
	boolean_t x;
	int i;

	/* First thing we do is attempt to lock the Master. */
	do
	{
		x = heaplib_lock_trylock(&heaplib_region_lock) == 0;
	} 
	while(!x);
	if(!x)
	{
		PRINTF("heaplib_walk: can't lock?\n");
		return;
	}

	for(i = 0; i < nelem(regions); i++)
	{
		PRINTF("walk: region=%d\n", i);
		heaplib_lock_lock(&regions[i].lock);
		__region_walk(&regions[i]);
		heaplib_lock_unlock(&regions[i].lock);
	}

#if 1 // XXX always set
	heaplib_lock_unlock(&heaplib_region_lock);
#endif
}

static void
__region_walk(heaplib_region_t * h)
{
	heaplib_node_t * n;

	PRINTF("walk region: free=%ld size=%ld addr=%p flags=%x free_list=%p nodes_free=%ld nodes_active=%ld\n",
		h->free,
		h->size,
		h->addr,
		h->flags,
		h->free_list,
		h->nodes_free,
		h->nodes_active);

	n = (heaplib_node_t * )h->addr;
	PRINTF("walk region addr=%p\n", n);

	while(heaplib_region_within(n, h))
	{
		if(n->active)
		{
			PRINTF("walk: node=%p payload=%p active=%d size=%ld task=%ld refs=%x flags=%lx\n",
				n,
				&n->payload[0],
				n->active,
				heaplib_node_size(n),
				n->pc_t.task,
				n->pc_t.refs,
				n->pc_t.flags);
		}
		else
		{
			PRINTF("walk: node=%p payload=%p active=%d size=%ld next=%p prev=%p\n",
				n,
				&n->payload[0],
				n->active,
				heaplib_node_size(n),
				n->free_t.next,
				n->free_t.prev);
		}

		n = heaplib_node_next(n);
	}
}

void
heaplib_lock_release(void)
{
#if 0 // XXX when 1: tesitng global lock
	/* use this to test the global lock */
	heaplib_lock_unlock(&heaplib_region_lock);
#endif
}

heaplib_error_t
heaplib_ptr2region(vaddr_t v, heaplib_region_t ** hp, heaplib_flags_t f)
{
	heaplib_error_t e;
	boolean_t x;
	vbaddr_t a;
	int i;

	/* First thing we do is attempt to lock the Master. Yield to flags */
	do
	{
		x = heaplib_lock_trylock(&heaplib_region_lock) == 0;
	} 
	while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;

	e = heaplib_error_fatal;
	for(i = 0; i < nelem(regions); i++)
	{
		heaplib_lock_lock(&regions[i].lock);

		if(regions[i].flags & heaplib_flags_active)
		{
			a = regions[i].addr;
			if((vbaddr_t)v >= a && (vbaddr_t)v < (a + regions[i].size))
			{
				e = heaplib_error_none;
				*hp = &regions[i];
				/* Don't unlock */
				break;
			}
		}

		heaplib_lock_unlock(&regions[i].lock);
	}

#if 1 // XXX when 0: test global lock
	heaplib_lock_unlock(&heaplib_region_lock);
#endif
	return e;
}

/**
 * \brief Retrieve the first matching Region for the specified flags.
 *
 * Scan through the Region list searching for a Region with matching flags and
 * return it locked.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 22, 2019
 */
heaplib_error_t
heaplib_region_find_first(heaplib_region_t ** rp, heaplib_flags_t f)
{
	heaplib_error_t e;
	boolean_t x;
	int i;

#if 1 // XXX this must always be set, even during global lock testing
	/* First thing we do is attempt to lock the Master. Yield to flags */
	do
	{
		x = heaplib_lock_trylock(&heaplib_region_lock) == 0;
	} 
	while(!x && (f & heaplib_flags_wait));
	if(!x)
	{
		PRINTF("ERROR: region_find_first: cant lock\n");
		return heaplib_error_again;
	}
#endif

	e = heaplib_error_fatal;
	/* Once locked, search for the next matching Region */
	for(i = 0; i < nelem(regions); i++)
	{
		e = __region_test_and_lock(&regions[i], f);
		if(e == heaplib_error_none)
		{
			*rp = &regions[i];
			break;
		}
	}

#if 1 // XXX when 0: test global lock
	heaplib_lock_unlock(&heaplib_region_lock);
#endif
	return e;
}

/**
 * \warning This must be called with the Master locked.
 * \warning We only test to see if flags match, allowing the caller to safely
 * 	    retrieve all Regions if they wish.
 */
static heaplib_error_t
__region_test_and_lock(heaplib_region_t * rp, heaplib_flags_t f)
{
	boolean_t x;

	PRINTF("thread=%ld: region\n", pthread_self());

	do {
		x = heaplib_lock_trylock(&(rp)->lock) == 0;
	}
	while(!x && (f & heaplib_flags_wait));
	if(!x)
	{
		PRINTF("ERROR: __region_test_and_lock cantlock\n");
		return heaplib_error_again;
	}

	/* The Region must be active and must not be restricted */
	if((rp->flags & heaplib_flags_active) != 0 && 
	   (rp->flags & heaplib_flags_restrict) == 0 &&
	   (rp->flags & heaplib_flags_busy) == 0)
	{
		/* Either no flags are set (get any) or the flags match exactly */
		if((f & heaplib_flags_regionmask) == 0 ||
	   	   ((f & heaplib_flags_regionmask) == 
		    (rp->flags & heaplib_flags_regionmask)))
		{
			/* We've got a match, so hold the lock */
			return heaplib_error_none;
		}
	}

	/* No match. Unlock */
	heaplib_lock_unlock(&(rp)->lock);
	return heaplib_error_fatal;
}

/**
 * \brief Retrieve the next matching Region for the specified flags.
 *
 * Scan through the Region list searching for a Region with matching flags and
 * return it locked. Presume the provided Region is locked and was previously
 * returned by this function or by _find_first. Use the base address to scan
 * upward in memory for subsequent regions.
 *
 * \warning This function expects a valid and *locked* Region in rp
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 22, 2019
 */
heaplib_error_t
heaplib_region_find_next(heaplib_region_t ** rp, heaplib_flags_t f)
{
	heaplib_error_t e;
	boolean_t x;
	vbaddr_t b;
	int i;
	int j;
	int k;

	/* First thing we do is release the current lock. */
	b = (*rp)->addr;
	heaplib_lock_unlock(&(*rp)->lock);

#if 1 // XXX when 0: test global lock
	/* Second thing we do is attempt to lock the Master. Yield to flags */
	do {
		x = heaplib_lock_trylock(&heaplib_region_lock) == 0;
	}
	while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;
#endif

	/* With the Master lock held, we can read the entire table without
	 * holding a lock, because the Regions themselves cannot be altered.
	 * So find the next viable candidate.
	 */
	k = -1;
	for(i = 0; i < nelem(regions); i++)
	{
		if(b >= regions[i].addr)
		{
			continue;
		}

		k = i;
		/* Now test for the closest match */
		for(j = i + 1; j < nelem(regions); j++)
		{
			if(regions[i].addr >= regions[j].addr)
			{
				continue;
			}

			k = j;
			break;
		}
	}

	/* No other region found */
	x = False;
	*rp = nil;
	if(k > -1)
	{
		/* Found! Now, attempt to hold the Region's lock */
		*rp = &regions[k];

		do {
			x = heaplib_lock_trylock(&regions[k].lock) == 0;
		}
		while(!x && (f & heaplib_flags_wait));
	}

	/* We're OK to unlock the master regardless of if we succeeded. */
#if 1 // XXX always unlock here
	heaplib_lock_unlock(&heaplib_region_lock);
#endif

	if(!x && k > -1)
		return heaplib_error_fatal;

	return (k == -1) ? heaplib_error_again : heaplib_error_none ;
}

/**
 * \brief Delete a region from memory.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
heaplib_error_t
heaplib_region_delete(heaplib_region_t r)
{
	/* XXX what the heck is the best way to do this to force migration or
	 * make apps/threads dealloc data in a region
	 */

	/* XXX !!! WARNING !!! XXX
		- each lock restricts a specific Region from being altered
		- but also helps ensure the adjacent Region is not damaged
		- this way to delete a node we must respect the locks of the
		  ADJACENT nodes as well because the prev ane nxt pointers
		  must be updated
		- this protects us so we ONLY have to lock one Region on a
		  calloc and DONT need the entire Region lock set
			- because if we Delete an adjacent Region, that Region
			  cannot be deleted until the adjacent Lock is unset

		PROTOCOL:
			- lock the target Region
			- set the Restrict and Busy flags
			- this is the signal that we are about to delete and
			  to Not Use
			- if an adjacent Lock holder has the Lock already,
			  we cannot Delete this node until they relinquish their
			  lock
			- so this means the next pointer in OUR locked Region is
			  still *valid*
			- so the person with the calloc Lock on the adjacent Region
			  can still use our next pointer to skip past a Restrict Region
	 */
	return heaplib_error_fatal; // XXX not implemented yet
}

/**
 * \brief Add a new memory region to the environment.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
heaplib_error_t
heaplib_region_add(vaddr_t a, size_t sz, heaplib_flags_t f)
{
	heaplib_footer_t * nf;
	heaplib_region_t * h;
	heaplib_node_t * n;
	int i;

	/* This process always waits */
	heaplib_lock_lock(&heaplib_region_lock);

	h = nil;
	for(i = 0; i < nelem(regions); i++)
	{
		if(!(regions[i].flags & heaplib_flags_active))
		{
			h = &regions[i];
			break;
		}
	}

	if(!h)
	{
		/* XXX warning here that no region is free */
	}
	else
	{
		heaplib_lock_init(&h->lock);
		h->flags = f | heaplib_flags_active;
		h->free = sz - (sizeof(*n) + sizeof(*nf));
		h->size = sz;
		h->addr = (vbaddr_t)a;
		h->nodes_active = 0;
		h->nodes_free = 1;

		/* Initialize the Region */
		n = (heaplib_node_t * )h->addr;
		h->free_list = n;

		__heaplib_node_init(h, n);
	}

	heaplib_lock_unlock(&heaplib_region_lock);

	return heaplib_error_none;
}

static void
__heaplib_node_init(heaplib_region_t * h, heaplib_node_t * n)
{
	heaplib_footer_t * f;

	n->size = h->free;
	n->free_t.next = nil;
	n->free_t.prev = nil;
	n->magic = HEAPLIB_MAGIC;

	f = heaplib_node_footer(n);
	f->magic = HEAPLIB_MAGIC;
	f->size = n->size;
}

