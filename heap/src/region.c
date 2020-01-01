#include "heaplib/heaplib.h"
#include <stdio.h>

static heaplib_region_t regions[NREGIONS];
static heaplib_lock_t heaplib_region_lock;

static void __region_walk(heaplib_region_t * );

static heaplib_error_t __region_test_and_lock(
				heaplib_region_t *,
				heaplib_flags_t);
static heaplib_error_t __region_scan_next_and_lock(
				heaplib_region_t **,
				vbaddr_t,
				heaplib_flags_t);

static void __heaplib_node_init(heaplib_region_t *, heaplib_node_t * );

void
heaplib_init(void)
{
	int i;

	for(i = 0; i < nelem(regions); i++)
	{
		heaplib_lock_init(&regions[i].lock);
	}

	heaplib_lock_init(&heaplib_region_lock);
}

void
heaplib_walk(void)
{
	boolean_t x;
	int i;

	/* First thing we do is attempt to lock the Master. */

	/* This will always succeed because we are guaranteed to wait. */
	heaplib_region_lock_flags(&heaplib_region_lock, heaplib_flags_wait);

	for(i = 0; i < nelem(regions); i++)
	{
		PRINTF("walk: region=%d\n", i);
		/* This debugging routine always waits */
		heaplib_lock_lock(&regions[i].lock);
		__region_walk(&regions[i]);
		heaplib_lock_unlock(&regions[i].lock);
	}

	heaplib_lock_unlock(&heaplib_region_lock);
}

static void
__region_walk(heaplib_region_t * h)
{
	heaplib_node_t * n;

	PRINTF(
		"walk region: free=%ld size=%ld addr=%p flags=%x free_list=%p "
		"nodes_free=%ld nodes_active=%ld\n",
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
			PRINTF(
				"walk: node=%p payload=%p active=%d size=%ld "
				"task=%ld refs=%x flags=%lx\n",
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
			PRINTF(
				"walk: node=%p payload=%p active=%d size=%ld "
				"next=%p prev=%p\n",
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

/**
 * \brief Convert a heap pointer to the Region that supports it.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 22, 2019
 */
heaplib_error_t
heaplib_ptr2region(vaddr_t v, heaplib_region_t ** hp, heaplib_flags_t f)
{
	heaplib_error_t e;
	vbaddr_t a;
	int i;

	/* First thing we do is attempt to lock the Master. Yield to flags */
	e = heaplib_region_lock_flags(&heaplib_region_lock, f);
	if(e != heaplib_error_none)
	{
		PRINTF("error: can't lock the master lock\n");
		return heaplib_error_again;
	}

	e = heaplib_error_fatal;
	for(i = 0; i < nelem(regions); i++)
	{
		e = heaplib_region_lock_flags(&regions[i].lock, f);
		if(e != heaplib_error_none)
		{
			PRINTF("ERROR: can't region lock in ptr2region\n");
			break;
		}

		if(regions[i].flags & heaplib_flags_active)
		{
			a = regions[i].addr;
			if((vbaddr_t)v >= a && 
			   (vbaddr_t)v < (a + regions[i].size))
			{
				e = heaplib_error_none;
				*hp = &regions[i];
				/* Don't unlock */
				break;
			}
		}

		heaplib_lock_unlock(&regions[i].lock);
	}

	heaplib_lock_unlock(&heaplib_region_lock);
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

	/* First thing we do is attempt to lock the Master. Yield to flags */
	e = heaplib_region_lock_flags(&heaplib_region_lock, f);
	if(e != heaplib_error_none)
	{
		PRINTF("ERROR: region_find_first: cant lock\n");
		return e;
	}

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

	heaplib_lock_unlock(&heaplib_region_lock);
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
	heaplib_error_t e;

	e = heaplib_region_lock_flags(&(rp)->lock, f);
	if(e != heaplib_error_none)
	{
		PRINTF("ERROR: __region_test_and_lock cantlock\n");
		return e;
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
	vbaddr_t b;

	/* First thing we do is release the current lock, which *must* be done
	 * without first holding the Master lock.
	 */
	b = (*rp)->addr;
	heaplib_lock_unlock(&(*rp)->lock);

	e = heaplib_region_lock_flags(&heaplib_region_lock, f);
	if(e != heaplib_error_none)
	{
		PRINTF("error: heaplib_region_find_next: cant lock master\n");
		return e;
	}

	/* This function will search for the next viable Region that matches
	 * our desired Flags, or it will return nothing. It's notable that
	 * this function will *not* return EAGAIN if we can't lock a Region
	 * because of a Flags issue (wait failure, etc). We only care if the
	 * attempt succeeded or not. If the caller wants to wait, they should
	 * explicitly ask to.
	 */
	e = __region_scan_next_and_lock(rp, b, f);

	heaplib_lock_unlock(&heaplib_region_lock);

	return e;
}

/**
 * \brief Scan for the next viable Region by ascending address.
 *
 * \warning Must be called with the master locked.
 *
 * \date January 1, 2020
 * \author Don A. Bailey <donb@labmou.se>
 */
static heaplib_error_t
__region_scan_next_and_lock(heaplib_region_t ** hp, vbaddr_t b, heaplib_flags_t f)
{
	heaplib_region_t * h;
	heaplib_error_t e;
	vbaddr_t n;
	int i;
	int j;

	*hp = nil;

	/* Search for a viable Next Region until we run out of Regions */
	do
	{
		n = b;
		h = nil;
		for(i = 0; i < nelem(regions); i++)
		{
			/* We order by increasing base address */
			if(b >= regions[i].addr)
			{
				continue;
			}

			/* Now, ensure the 'n'ew address is lower in memory than
		 	* anything new we'd like to test.
		 	*/
			if(n > b && regions[i].addr >= n)
			{
				continue;
			}

			n = regions[i].addr;
			/* Now test for the closest match */
			for(j = i + 1; j < nelem(regions); j++)
			{
				if((regions[j].addr < n) && 
				   (regions[j].addr > b))
				{
					n = regions[j].addr;
					h = &regions[i];
				}
			}
		}

		/* No suitable Region above our Base was found */
		if(!h)
			break;

		/* Save the base address for the next round */
		b = h->addr;

		/* If we found our Region, return */
		e = __region_test_and_lock(h, f);
		if(e == heaplib_error_none)
		{
			/* We are locked and ready */
			*hp = h;
			return e;
		}
	}
	while(True);

	return heaplib_error_fatal;
}

/**
 * \brief Perform the actual delete function
 *
 * If all of the allocations are free'd, perform the actual Delete. This should
 * be called by the free function.
 *
 * \warning This should be called with the Region locked.
 * \warning This should be called by free()
 *
 * \date January 1, 2020
 * \author Don A. Bailey <donb@labmou.se>
 */
void
__heaplib_region_delete_internal(heaplib_region_t * h)
{
	if((h->flags & heaplib_flags_restrict) &&
	   (h->free == h->size) &&
	   (h->nodes_active == 0))
	{
		h->free_list = nil;
		h->nodes_free = 0;
		h->addr = nil;
		h->flags = 0;
		h->size = 0;
		h->free = 0;
	}
}

/**
 * \brief Delete a region from memory.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
heaplib_error_t
heaplib_region_delete(heaplib_region_t * h)
{
	heaplib_error_t e;
	int i;

	e = heaplib_region_lock_flags(&heaplib_region_lock, heaplib_flags_wait);
	if(e != heaplib_error_none)
	{
		PRINTF("error: heaplib_region_add: can't lock master\n");
		return e;
	}

	/* Make sure the Region is real */
	e = heaplib_error_fatal;
	for(i = 0; i < nelem(regions); i++)
	{
		/* Found it. Because we hold the Master lock, we can change
		 * the flags even if another thread holds the lock for this
		 * Region. If another thread is allocating within this Region,
		 * that's fine. We just won't see any allocations after this
		 * action.
		 */
		if(h == &regions[i])
		{
			h->flags |= heaplib_flags_restrict;
			e = heaplib_error_none;
			break;
		}
	}

	heaplib_lock_unlock(&heaplib_region_lock);

	return e;
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
	heaplib_error_t e;
	int i;

	e = heaplib_region_lock_flags(&heaplib_region_lock, f);
	if(e != heaplib_error_none)
	{
		PRINTF("error: heaplib_region_add: can't lock master\n");
		return e;
	}

	h = nil;
	for(i = 0; i < nelem(regions); i++)
	{
		e = heaplib_region_lock_flags(&regions[i].lock, f);
		if(e == heaplib_error_none)
		{
			if(!(regions[i].flags & heaplib_flags_active))
			{
				h = &regions[i];
				break;
			}

			heaplib_lock_unlock(&regions[i].lock);
		}
	}

	e = heaplib_error_none;
	if(!h)
	{
		PRINTF("error: no region is free\n");
		e = heaplib_error_fatal;
	}
	else
	{
		h->free = sz - (sizeof(*n) + sizeof(*nf));
		h->flags = f | heaplib_flags_active;
		h->size = sz;
		h->addr = (vbaddr_t)a;
		h->nodes_active = 0;
		h->nodes_free = 1;

		/* Initialize the Region */
		n = (heaplib_node_t * )h->addr;
		h->free_list = n;

		__heaplib_node_init(h, n);

		heaplib_lock_unlock(&h->lock);
	}

	heaplib_lock_unlock(&heaplib_region_lock);

	return heaplib_error_none;
}

static void
__heaplib_node_init(heaplib_region_t * h, heaplib_node_t * n)
{
	heaplib_footer_t * f;

	n->size = h->free & ~1;
	n->free_t.next = nil;
	n->free_t.prev = nil;
	n->magic = HEAPLIB_MAGIC;

	f = heaplib_node_footer(n);
	f->magic = HEAPLIB_MAGIC;
	f->size = n->size;
}

