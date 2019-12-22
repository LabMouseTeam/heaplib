#include "heaplib/heap.h"

static heaplib_lock_t alloc_lock;

static heaplib_error_t __heaplib_calloc(vaddr_t *, size_t, heaplib_flags_t);

heaplib_error_t
heaplib_calloc(vaddr_t * vp, size_t x, size_t y, heaplib_flags_t f)
{
	boolean_t r;
	size_t z;
	size_t c;

	/* First, check overflow */
	z = x * y;
	if(z < x || z < y)
	{
		/* XXX error */
		return False;
	}

	/* Now, round up by chunks */
	c = HEAPLIB_B2C(z);
	z = HEAPLIB_C2B(c);

	/* Check for overflow again */
	if(z < x || z < y)
	{
		/* XXX error */
		return False;
	}

	return __heaplib_calloc(vp, z, f);
}

/**
 * \brief Allocate cleared (zeroed) memory.
 *
 * Search each region for a usable chunk of memory fitting the request, and
 * attempt to allocate a node within that region.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static heaplib_error_t
__heaplib_calloc(vaddr_t * vp, size_t z, heaplib_flags_t f)
{
	heaplib_region_t * h;
	heaplib_region_t * n;
	heaplib_error_t e;
	heaplib_flags_t m;
	boolean_t r;

	*vp = nil;

	e = heaplib_region_acquire(&h, f);
	if(e != heaplib_error_none)
	{
		return e;
	}

	/* We can go ahead and release the Region lock since we are not doing
	 * any work on the Region level, and we're guaranteed the Region
	 * specific Lock.
	 */
	heaplib_region_release();

	/* Cycle through each region looking for a match that:
	 *	1) Supports the requested flags
	 *	2) Supports the request size
	 */
	m = f & heaplib_flags_regionmask;

	/* We're always guaranteed at least one region */
	r = False;

	/* We've been guaranteed that we have locked the first Region at this
	 * point, either immediately or by waiting for it.
	 */

	do {
		/* Match flags, ensure the Region isn't being deprecated, 
		 * verify that the Region is active, and ensure it isn't
		 * being updated by a different thread.
		 *
		 * Ensure this Region has enough free bytes (they may not
		 * be contiguous)
		 */
		if((h->flags & heaplib_flags_regionmask) == m &&
		   (h->flags & heaplib_flags_restrict) == 0 &&
		   (h->flags & heaplib_flags_active) != 0 &&
		   (h->flags & heaplib_flags_busy) == 0 &&
		   (h->free > z))
		{
			/* We have enough RAM and the flags are correct. */
			r = __heaplib_calloc_region(h, vp, z, f);
		}

		/* Because own this Region's lock, we are guaranteed that the
		 * next Region will not be altered out from under us.
		 */
		n = heaplib_region_next(h);
		heaplib_unlock(&h->lock);
		h = n;
	}
	while(h && !r);

	heaplib_region_release();

	return r;
}

/**
 * \brief Attempt to allocate within a specific Region.
 *
 * \warning We must be very careful with what threads are allowed to use the
 * heaplib_flags_wait flag. Because the Region lock is held, a thread that is
 * not of significant priority can lock the entire system waiting for memory
 * to be freed. This can lead to deadlock. Only critical threads should be
 * able to use Wait, and it should be used sparingly.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static boolean_t
__heaplib_calloc_region(
	heaplib_region_t * r,
	vaddr_t * vp,
	size_t z,
	heaplib_flags_t f)
{
	do
	{
		
	}
	while(f & heaplib_flags_wait);
}

