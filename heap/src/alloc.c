#include "heaplib/heap.h"

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
				heaplib_flags_t f)

heaplib_error_t
heaplib_free(vaddr_t * vp, heaplib_flags_t f)
{
	heaplib_footer_t * af;
	heaplib_region_t * h;
	heaplib_node_t * L;
	heaplib_node_t * a;
	heaplib_error_t e;
	vaddr_t v;

	v = *vp;
	*vp = nil;

	// XXX
	// change this to go backward if we are beyond the midpoint

	/* Make sure the pointer is valid */
	e = heaplib_region_by_ptr(v, &h, f);
	if(e != heaplib_error_none)
	{
		return e;
	}

	/* The Region is returned locked */
	L = nil;
	a = (heaplib_node_t * )h->addr;
	while((caddr_t)a < (caddr_t)((caddr_t)a + h->size))
	{
		/* Save the Last (most recently observed) Free node */
		if(!a->active)
			L = a;

		if(v == (vaddr_t)&a->payload[0])
		{
			if(!a->active)
			{
				// XXX warning! should be active!
				heaplib_unlock(&h->lock);
				return heaplib_error_fatal;
			}

			a->active = False;

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

			heaplib_unlock(&h->lock);

			return heaplib_error_none;
		}

		a = heaplib_node_next(a);
	}

	/* Not found */
	heaplib_unlock(&h->lock);
	return heaplib_error_fatal;
}

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
	heaplib_error_t e;

	*vp = nil;

	e = heaplib_region_find_first(&h, f);
	if(e != heaplib_error_none)
	{
		return e;
	}

	/* We've been guaranteed that we have locked the first Region at this
	 * point, either immediately or by waiting for it.
	 */

	do {
		/* Ensure this Region has enough free bytes (they may not
		 * be contiguous)
		 */
		if(h->free >= z)
		{
			/* We have enough RAM and the flags are correct. */
			e = __heaplib_calloc_with_coalesce(h, vp, z, f);
			if(e == heaplib_error_none)
			{
				return e;
			}
		}

		e = heaplib_region_find_next(&h, f);
	}
	while(h && e == heaplib_error_none);

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
		if(e == heaplib_error_none)
		{
			return e;
		}

		/* If we couldn't alloc, attempt to coalesce since we know
		 * there are ample bytes, they just may not be adjacent.
		 */
		e = __heaplib_coalesce(h, f, &j);
	}

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

	b = nil;
	a = h->free_list;
	while(a != nil && ((caddr_t)a < (caddr_t)((caddr_t)a + h->size)))
	{
		if(!b)
		{
			b = a;
			a = a->next;
		}
		else
		{
			if(heaplib_node_next(b) != a)
			{
				b = a;
				a = a->next;
			}
			else
			{
				/* The nodes are adjacent so merge them */
				h->free += sizeof(*a) + sizeof(*bf);

				/* Consume the higher node */
				b->free_t.next = a->free_t.next;
				if(b->free_t.next)
					b->free_t.next->free_t.prev = b;

				b->size += heaplib_node_size(a) +
						sizeof(*a) +
						sizeof(*bf);

				bf = heaplib_node_footer(b);
				bf->size = heaplib_node_size(b);

				a = heaplib_node_next(b);

				j++;
			}
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

	b = nil;
	a = nil;
	n = h->free_list;
	while(n && n->active == False)
	{
		if(heaplib_node_size(n) >= z && 
		   ((n->pc_t.flags & heaplib_flags_dontuse) == 0))
		{
			if(heaplib_node_size(n) == z || 
			   (heaplib_node_size(n) - z) < HEAPLIB_MIN_NODE)
			{
				a = n;
			}
			else
			{
				/* There's ample room to perform node split */
				b = (heaplib_node_t * )&n->payload[z + sizeof(heaplib_footer_t)];

				/* Temporarily add 'b' to the free list so it
				 * can get adjusted properly later
				 */
				b->free_t.next = n->free_t.next;
				n->free_t.next = b;
				b->free_t.prev = n;
				if(b->free_t.next)
					b->free_t.next->prev = b;

				b->size = heaplib_node_size(n) - z - (sizeof(heaplib_node_t) +
									sizeof(heaplib_footer_t));
				bf = (heaplib_footer_t * )&b->payload[heaplib_node_size(b)];
				bf->size = heaplib_node_size(b);
				bf->magic = HEAPLIB_MAGIC;

				n->size = z;

				bf = (heaplib_footer_t * )&n->payload[heaplib_node_size(n)];
				bf->size = heaplib_node_size(n);
				bf->magic = HEAPLIB_MAGIC;

				h->free -= (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));

				a = n;
			}

			break;
		}

		n = n->free_t.next;
	}

	if(!a)
	{
		*vp = nil;
	}
	else
	{
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
		a->pc_t.region = h->id;
		a->pc_t.flags = f;
		a->pc_t.refs = 1;

		a->active = True;

		h->free -= heaplib_node_size(a);
	}

	return (a == nil) ? heaplib_error_fatal : heaplib_error_none ;
}

