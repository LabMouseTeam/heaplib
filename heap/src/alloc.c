#include "heaplib/heaplib.h"
#include <string.h>
#include <stdio.h>

/* XXX convert this to use ptr2node.c */

static heaplib_lock_t alloc_lock;

static heaplib_error_t __heaplib_coalesce(heaplib_region_t *, heaplib_flags_t, int * );
static heaplib_error_t __heaplib_calloc(vaddr_t *, size_t, heaplib_flags_t);
static heaplib_error_t __heaplib_calloc_within_region(
				heaplib_region_t *,
				vaddr_t *,
				size_t,
				heaplib_flags_t);
static heaplib_error_t __heaplib_calloc_with_coalesce(
				heaplib_region_t * h,
				vaddr_t * vp,
				size_t z,
				heaplib_flags_t f);

heaplib_error_t
heaplib_free(vaddr_t * vp, heaplib_flags_t f)
{
	heaplib_footer_t * af;
	heaplib_region_t * h;
	heaplib_node_t * L;
	heaplib_node_t * a;
	heaplib_error_t e;
	vaddr_t v;
	int num;

	v = *vp;
	*vp = nil;

	// XXX
	// change this to go backward if we are beyond the midpoint

	/* Make sure the pointer is valid */
	PRINTF("free: find region\n");
	e = heaplib_ptr2region(v, &h, f);
	if(e != heaplib_error_none)
	{
		PRINTF("free: cant find region: %d\n", e);
		return e;
	}

	PRINTF("free: spinning\n");
	/* The Region is returned locked */
	num = 0;
	L = nil;
	a = (heaplib_node_t * )h->addr;
	while(heaplib_region_within(a, h))
	{
		/* Save the Last (most recently observed) Free node */
		if(!a->active)
			L = a;

		if(a->magic != HEAPLIB_MAGIC)
		{
			PRINTF("ERROR: heaplib magic failure at node=%d/%p\n", num, a);
			heaplib_lock_unlock(&h->lock);
			heaplib_lock_release(); // XXX lock testing
			return heaplib_error_fatal;
		}
		num++;

		if(v == (vaddr_t)&a->payload[0])
		{
			if(!a->active)
			{
				// XXX warning! should be active!
				PRINTF("ERROR: free on a active node? %p\n", v);
				heaplib_lock_unlock(&h->lock);
				heaplib_lock_release(); // XXX lock testing
				return heaplib_error_fatal;
			}

			af = heaplib_node_footer(a);
			if(a->magic != HEAPLIB_MAGIC || af->magic != HEAPLIB_MAGIC)
			{
				PRINTF("ERROR: magic is corrupt for node=%p\n", a);
				heaplib_lock_unlock(&h->lock);
				heaplib_lock_release(); // XXX lock testing
				return heaplib_error_fatal;
			}

			a->active = False;

			// XXX The size can never be zero, dummy
			// af->size = 0;
			// a->size = 0;

			/* Place the node back in the list */
			if(!L)
			{
				/* Found a node lower in memory than free_list
				 * or free_list has not yet been set.
				 */
				a->free_t.next = h->free_list;
				a->free_t.prev = nil;
				if(h->free_list)
					h->free_list->free_t.prev = a;
				h->free_list = a;
			}
			else
			{
				a->free_t.next = L->free_t.next;
				L->free_t.next = a;
				a->free_t.prev = L;
				if(a->free_t.next)
					a->free_t.next->free_t.prev = a;
			}

			h->free += heaplib_node_size(a);
			h->nodes_active -= 1;
			h->nodes_free += 1;

			//PRINTF("free: heaplib_error_none\n");
			heaplib_lock_unlock(&h->lock);
			heaplib_lock_release(); // XXX lock testing
			return heaplib_error_none;
		}

		a = heaplib_node_next(a);
	}

	/* Not found */
	PRINTF("free: heaplib_error_fatal\n");
	heaplib_lock_unlock(&h->lock);
	heaplib_lock_release(); // XXX lock testing
	return heaplib_error_fatal;
}

heaplib_error_t
heaplib_calloc(vaddr_t * vp, size_t x, size_t y, heaplib_flags_t f)
{
	heaplib_error_t e;
	size_t z;
	size_t c;

	/* First, check overflow */
	z = x * y;
	if(z < x || z < y)
	{
		/* XXX error */
		return heaplib_error_fatal;
	}

	/* Now, round up by chunks */
	c = HEAPLIB_B2C(z);
	z = HEAPLIB_C2B(c);

	/* Check for overflow again */
	if(z < x || z < y)
	{
		/* XXX error */
		return heaplib_error_fatal;
	}

	e = __heaplib_calloc(vp, z, f);
	PRINTF("__heaplib_calloc: thread=%ld e=%d *vp=%p sz=%ld \n", pthread_self(), e, *vp, z);
	return e;
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

	*vp = nil;

	PRINTF("__heaplib_calloc: z=%ld\n", z);

	e = heaplib_region_find_first(&h, f);
	if(e != heaplib_error_none)
	{
		PRINTF("__heaplib_calloc: region_find_first %d\n", e);
		return e;
	}

	/* We've been guaranteed that we have locked the first Region at this
	 * point, either immediately or by waiting for it.
	 */

	do {
		/* Ensure this Region has enough free bytes (they may not
		 * be contiguous)
		 */
		if(h->free >= z && __validate_region_request(h, z))
		{
			PRINTF("__heaplib_calloc: found h->free > z\n");

			/* We have enough RAM and the flags are correct. */
			e = __heaplib_calloc_with_coalesce(h, vp, z, f);
			if(e == heaplib_error_none)
			{
				PRINTF("__heaplib_calloc: calloc_w_coal\n");

				heaplib_lock_unlock(&h->lock);
				heaplib_lock_release(); // XXX lock testing
				return e;
			}
		}

		e = heaplib_region_find_next(&h, f);
	}
	while(h && e == heaplib_error_none);

	/* heaplib_region_find_next unlocks the last Region on our behalf, so
	 * if we arrive here, there is no Region left to unlock. This also 
	 * always unlocks the global lock.
	 */

	PRINTF("__heaplib_calloc: generic? %d\n", e);
	return e == heaplib_error_again ? 
		heaplib_error_again :
		heaplib_error_fatal;
}

/**
 * \brief Keep attempting to allocate memory while coalesce succeeds.
 *
 * Since the coalesce operation may take multiple runs, but is guaranteed to
 * be a finite operation, keep attempting to allocate and coalesce at every
 * failure. We know this is OK if the Region has enough free bytes to support
 * the operation, but the calloc fails. Typically we will see one of four
 * runs occur:
 *	- calloc (succeeds)
 *	- calloc; coalesce; (coalesce combined no nodes)
 *	- calloc; coalesce; calloc (coalesce combined and calloc suecceeded)
 *	- calloc; coalesce; calloc; coalesce; (can combine, but calloc fails)
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static heaplib_error_t
__heaplib_calloc_with_coalesce(
	heaplib_region_t * h,
	vaddr_t * vp,
	size_t z,
	heaplib_flags_t f)
{
	heaplib_error_t e;
	int j;

	/* We loop in coalesce which we know is finite, in case our
	 * algorithm needs multiple runs to complete.
	 */
	j = 1;
	while(j > 0)
	{
		/* Always just attempt to alloc, first */
		e = __heaplib_calloc_within_region(h, vp, z, f);

		/* If we couldn't alloc, or the heap is too fragmented, coalesce */
		if(e != heaplib_error_none ||
		  ((h->nodes_free > h->nodes_active) && ((h->free * 100) / h->size >= 60)))
		{
			/* If we couldn't alloc, attempt to coalesce since we know
		 	 * there are ample bytes, they just may not be adjacent.
			 * If the balance of the heap is tilted, attempt to adjust.
		 	 */
			if(e == heaplib_error_none)
			{
				PRINTF("FORCED COALESCE!\n");
			}

			__heaplib_coalesce(h, f, &j);
		}

		if(e == heaplib_error_none)
		{
			PRINTF("__heaplib_calloc_with_coalesce: yay!\n");
			return e;
		}
	}

	PRINTF("__heaplib_calloc_with_coalesce: hmm!\n");
	return heaplib_error_fatal;
}

/**
 * \brief Coalesce the heap.
 *
 * \param jp [out] The total number of joins.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static heaplib_error_t
__heaplib_coalesce(heaplib_region_t * h, heaplib_flags_t f, int * jp)
{
	heaplib_footer_t * bf;
	heaplib_node_t * a;
	heaplib_node_t * b;
	int j;

	if(jp)
		*jp = 0;

#if 0
	/* XXX */
	return heaplib_error_none;
	/* XXX */
#endif

	PRINTF("__heaplib_coalesce: try\n");

	a = nil;
	b = h->free_list;
	if(b)
		a = heaplib_free_next(b);

	while(a && heaplib_region_within(a, h))
	{
		if(heaplib_node_next(b) != a)
		{
			b = a;
			a = heaplib_free_next(a);
		}
		else
		{
			/* The nodes are adjacent so merge them */
			h->free += sizeof(*a) + sizeof(*bf);

			/* Consume the higher node */
			heaplib_free_next(b) = heaplib_free_next(a);
			if(heaplib_free_next(b))
				heaplib_free_prev(heaplib_free_next(b)) = b;

			PRINTF("coal: CONSUME size=%lu\n", b->size);

			b->size += heaplib_node_size(a) +
					sizeof(*a) +
					sizeof(*bf);
			PRINTF("coal: CONSUME NOW size=%lu\n", b->size);

			bf = heaplib_node_footer(b);
			bf->size = heaplib_node_size(b);

			a = heaplib_free_next(b);

			h->nodes_free -= 1;

			j++;
		}
	}

	if(jp)
		*jp = j;

	return heaplib_error_none;
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
static heaplib_error_t
__heaplib_calloc_within_region(
	heaplib_region_t * h,
	vaddr_t * vp,
	size_t z,
	heaplib_flags_t f)
{
	heaplib_footer_t * bf;
	heaplib_node_t * n;
	heaplib_node_t * a;
	heaplib_node_t * b;
	size_t o;

	b = nil;
	a = nil;
	n = h->free_list;
	while(n)
	{
		if(n->active)
		{
			PRINTF("ERROR: active node in the free list!\n");
			return heaplib_error_fatal;
		}

		if(heaplib_node_size(n) >= z)
		{
			if(heaplib_node_size(n) == z || 
			   (heaplib_node_size(n) - z) < HEAPLIB_MIN_NODE)
			{
				PRINTF("node expand!\n");
				a = n;
			}
			else
			{
				PRINTF("node: split! original node size=%ld\n", heaplib_node_size(n));

				/* There's ample room to perform node split */
				o = heaplib_node_size(n);
				n->size = z;
				b = heaplib_node_next(n);

				/* Temporarily add 'b' to the free list so it
				 * can get adjusted properly later
				 */
				b->free_t.next = n->free_t.next;
				n->free_t.next = b;
				b->free_t.prev = n;
				if(b->free_t.next)
					b->free_t.next->free_t.prev = b;

				b->magic = HEAPLIB_MAGIC;

				b->size = o - z - (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));

				PRINTF("split: new node size=%ld\n", b->size);

				bf = heaplib_node_footer(b);
				bf->size = heaplib_node_size(b);
				bf->magic = HEAPLIB_MAGIC;

				bf = heaplib_node_footer(n);
				bf->size = heaplib_node_size(n);
				bf->magic = HEAPLIB_MAGIC;

				h->free -= (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));
				h->nodes_free += 1;

				a = n;
			}

			break;
		}

		n = heaplib_free_next(n);
	}

	if(!a)
	{
		PRINTF("miss!\n");
		*vp = nil;
	}
	else
	{
		PRINTF("found! node=%p size=%ld \n", a, a->size);

		*vp = (vaddr_t)&a->payload[0];

		/* Now we can safely alter the free list */
		if(a->free_t.prev)
			a->free_t.prev->free_t.next = a->free_t.next;
		if(a->free_t.next)
			a->free_t.next->free_t.prev = a->free_t.prev;
		if(h->free_list == a)
			h->free_list = a->free_t.next;

		memset(&a->payload[0], 0, a->size);

		a->pc_t.task = GET_PLATFORM_TASKID();
		a->pc_t.flags = f;
		a->pc_t.refs = 1;

		a->magic = HEAPLIB_MAGIC;
		a->active = True;

		h->free -= heaplib_node_size(a);
		h->nodes_active += 1;
		h->nodes_free -= 1;
	}

	return (a == nil) ? heaplib_error_fatal : heaplib_error_none ;
}

