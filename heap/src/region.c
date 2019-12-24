#include "heaplib/heaplib.h"

static heaplib_region_t regions[NREGIONS];

static heaplib_lock_t heaplib_region_lock;

static heaplib_error_t __region_test_and_lock(heaplib_region_t *, heaplib_flags_t);

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
	do
	{
		x = heaplib_trylock(&heaplib_region_lock) == 0;
	} 
	while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;

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

	heaplib_unlock(&heaplib_region_lock);
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

	do {
		x = heaplib_trylock(&(rp)->lock) == 0;
	}
	while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;

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
	heaplib_unlock(&(rp)->lock);
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
	heaplib_unlock(&(*rp)->lock);

	/* Second thing we do is attempt to lock the Master. Yield to flags */
	do {
		x = heaplib_trylock(&heaplib_region_lock) == 0;
	}
	while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;

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
	if(k == -1)
	{
		heaplib_unlock(&heaplib_region_lock);
		return heaplib_error_fatal;
	}

	/* Found! Now, attempt to hold the Region's lock */
	*rp = &regions[k];

	do {
		x = heaplib_trylock(&region[k].lock) == 0;
	}
	while(!x && (f & heaplib_flags_wait));

	/* We're OK to unlock the master regardless of if we succeeded. */
	heaplib_unlock(&heaplib_region_lock);

	if(!x)
	{
		*rp = nil;
		return heaplib_error_again;
	}

	return heaplib_error_none;
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
	heaplib_region_t * h;
	heaplib_node_t * n;

	/* This process always waits */
	heaplib_lock(&heaplib_region_lock);

	h = nil;
	for(i = 0; i < nelem(regions); i++)
	{
		if(!(regions[i].flags & active))
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
		h->free = sz;
		h->size = sz;
		h->addr = a;
		h->id = i;

		/* Initialize the Region */
		n = (heaplib_node_t * )h->addr;
		h->free_list = n;
		/* XXX generate new Node */
	}

	heaplib_unlock(&heaplib_region_lock);

	return heaplib_error_none;
}

