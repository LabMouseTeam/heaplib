#include "heaplib/heap.h"

static heaplib_region_t regions[NREGIONS];

static heaplib_lock_t heaplib_region_lock;

static boolean_t __heaplib_region_alloc(vaddr_t, size_t, heaplib_flags_t);

/**
 * \brief Acquire the pointer to our Regions.
 *
 * \warning This function acquires the Region lock and does not release it.
 * Please use the release function to properly release the Region lock once
 * a specific region has (or all have) been used.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
heaplib_error_t
heaplib_region_acquire(heaplib_region_t ** rp, heaplib_flags_t f)
{
	heaplib_error_t e;
	boolean_t x;

	do {
		x = heaplib_trylock(&heaplib_region_lock) == 0;
	} while(!x && (f & heaplib_flags_wait));
	if(!x)
		return heaplib_error_again;

	*rp = &regions[0];

	/* We have to return with the first Region locked. Note that because
	 * we always need *one* Region, and the first Region is *always* the
	 * built-in SRAM region (or the closest thing to it), this Region
	 * may *never* be deleted. That's why the first node is not a pointer.
	 * Thus, we can always safely wait unless we've been directed not to.
	 */

	/* This returns Again if the lock is locked and we aren't waiting */
	e = heaplib_region_trylock(&(*rp)->lock, (f & heaplib_flags_wait));

	/* If we didn't acquire the Region's lock, unlock the master */
	if(e != heaplib_error_none)
		heaplib_unlock(&heaplib_region_lock);

	return e;
}

/**
 * \brief Release the region lock.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
void
heaplib_region_release(void)
{
	heaplib_unlock(&heaplib_region_lock);
}

/**
 * \brief Delete a region from memory.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
boolean_t
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
}

/**
 * \brief Add a new memory region to the environment.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
boolean_t
heaplib_region_add(vaddr_t a, size_t sz, heaplib_flags_t f)
{
	heaplib_region_t * v;
	heaplib_region_t * n;
	boolean_t r;

	heaplib_lock(&heaplib_region_lock);

	r = True;
	if(regions.flags & heaplib_flags_active)
	{
		r = __heaplib_region_alloc(&v);
	}
	else
	{
		v = &regions;
	}

	if(r)
	{
		heaplib_lock_init(&v->lock);

		v->flags = f | heaplib_flags_active;
		v->next = nil;
		v->size = sz;
		v->addr = a;

		if(v != &regions)
		{
			n = &regions;
			while(n->next)
				n = n->next;
			n->next = v;
			v->prev = n;
		}
	}

	heaplib_unlock(&heaplib_region_lock);
	return r;
}

/**
 * \brief Allocate a new Region.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static boolean_t
__heaplib_region_alloc(heaplib_region_t ** vp)
{
	heaplib_region_t * v;
	heaplib_region_t * x;
	boolean_t r;

	/* Require the node to be Internal */
	r = heaplib_calloc(
		&v,
		1,
		sizeof *v,
		(heaplib_flags_internal | heaplib_flags_wait));
	if(!r)
		return False;

	*vp = v;

	x = &regions;
	do
	{
		if(!x->next)
			x->next = v;

		x = x->next;
	}
	while(x);

	return r;
}

