/**
 * \file heap/src/alloc.c
 *
 * \brief Memory allocation routines for heaplib.
 *
 * Allocate and deallocate memory within a specific memory region. Use flags
 * to adjust how this occurs. Enable natural alignment to ensure that each
 * address can be used appropriately. Coalesce on demand to improve reaction.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 22, 2020
 */
#include "heaplib/heaplib.h"

static heaplib_error_t __heaplib_calloc_do_split(
				heaplib_region_t *,
				heaplib_node_t *,
				heaplib_node_t **,
				size_t);

static heaplib_error_t __heaplib_calloc_do_natural(
				heaplib_region_t *,
				heaplib_node_t *,
				heaplib_node_t **,
				size_t);

static heaplib_error_t __heaplib_calloc_try_natural(
				heaplib_region_t *,
				heaplib_node_t *,
				heaplib_node_t **,
				size_t,
				heaplib_flags_t);

static heaplib_error_t __heaplib_calloc_with_coalesce(
				heaplib_region_t *,
				vaddr_t *,
				size_t,
				heaplib_flags_t);

static heaplib_error_t __heaplib_calloc_within_region(
				heaplib_region_t *,
				vaddr_t *,
				size_t,
				heaplib_flags_t);

static heaplib_error_t __heaplib_coalesce(heaplib_region_t *, int * );
static heaplib_error_t __heaplib_calloc(vaddr_t *, size_t, heaplib_flags_t);

/**
 * \brief Free a node.
 *
 * \param vp [in] The pointer to free.
 * \param f [in] Flags.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 23, 2020
 */
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
			PRINTF("error: magic failure at node=%d/%p\n", num, a);
			heaplib_lock_unlock(&h->lock);
			return heaplib_error_fatal;
		}
		num++;

		if(v == (vaddr_t)&a->payload[0])
		{
			if(!a->active)
			{
				PRINTF("error: free on active node? %p\n", v);
				heaplib_lock_unlock(&h->lock);
				return heaplib_error_fatal;
			}

			af = heaplib_node_footer(a);
			if(a->magic != HEAPLIB_MAGIC || 
			   af->magic != HEAPLIB_MAGIC)
			{
				PRINTF("error: magic corrupt; node=%p\n", a);
				heaplib_lock_unlock(&h->lock);
				return heaplib_error_fatal;
			}

			a->active = False;

			/* Place the node back in the list */
			if(!L)
			{
				/* Found a node lower in memory than free_list
				 * or free_list has not yet been set.
				 */
				heaplib_free_next(a) = h->free_list;
				heaplib_free_prev(a) = nil;
				if(h->free_list)
					heaplib_free_prev(h->free_list) = a;
				h->free_list = a;
			}
			else
			{
				heaplib_free_next(a) = heaplib_free_next(L);
				heaplib_free_next(L) = a;
				heaplib_free_prev(a) = L;
				if(heaplib_free_next(a))
					heaplib_free_prev(
						heaplib_free_next(a)) = a;
			}

			h->free += heaplib_node_size(a);
			h->nodes_active -= 1;
			h->nodes_free += 1;

			/* Only force coalesce if we are surrounded, otherwise
			 * occurrence is too high 
			 */
			L = heaplib_node_prev(a);
			if((heaplib_region_within(a, h) && 
			   !heaplib_node_next(a)->active) &&
			   (heaplib_region_within(L, h) && !L->active))
			{
				PRINTF("WARN: forced free coalesce\n");
				__heaplib_coalesce(h, nil);
			}

			/* If all memory is free'd and we're restricted,
			 * perform the actual Delete operation. Even if we
			 * delete the Region, the lock stays live.
			 */
			__heaplib_region_delete_internal(h);

			heaplib_lock_unlock(&h->lock);
			return heaplib_error_none;
		}

		a = heaplib_node_next(a);
	}

	/* Not found */
	PRINTF("free: heaplib_error_fatal\n");
	heaplib_lock_unlock(&h->lock);
	return heaplib_error_fatal;
}

/**
 * \brief Allocate memory.
 *
 * \param vp [out] The allocated payload base address.
 * \param x [in] Scale of allocation.
 * \param y [in] Size of allocation.
 * \param f [in] Allocation flags.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 22, 2020
 */
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

	PRINTF("__heaplib_calloc: thread=%ld e=%d *vp=%p sz=%ld \n",
		pthread_self(),
		e,
		*vp,
		z);

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

	return e;
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

		/* If we couldn't alloc, or heap is too fragmented, coalesce */
		if(e != heaplib_error_none ||
		  ((h->nodes_free > h->nodes_active) && 
		  ((h->free * 100) / h->size >= 60)))
		{
			/* If we couldn't alloc, attempt coalesce since we know
		 	 * there are ample bytes they just may not be adjacent.
			 * If the balance of the heap is tilted,
			 * attempt to adjust.
		 	 */
			if(e == heaplib_error_none)
			{
				PRINTF("FORCED COALESCE!\n");
			}

			__heaplib_coalesce(h, &j);
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
 * \param h [in] The region to coalesce.
 * \param f [in] Flags.
 * \param jp [out] The total number of joins.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 20, 2019
 */
static heaplib_error_t
__heaplib_coalesce(heaplib_region_t * h, int * jp)
{
	heaplib_footer_t * bf;
	heaplib_node_t * a;
	heaplib_node_t * b;
	int j;

	j = 0;
	if(jp)
		*jp = 0;

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
	heaplib_node_t * n;
	heaplib_node_t * o;
	heaplib_error_t r;

	o = nil;
	n = h->free_list;
	while(n)
	{
		if(!heaplib_region_within(n, h))
		{
			PRINTF("error: ain't got nothin\n");
			return heaplib_error_fatal;
		}

		if(n->active)
		{
			PRINTF("error: active node in the free list!\n");
			return heaplib_error_fatal;
		}

		if(heaplib_node_size(n) >= z)
		{
			r = __heaplib_calloc_try_natural(h, n, &o, z, f);
			if(r == heaplib_error_none)
			{
				break;
			}

			PRINTF("error: size matched but can't alloc\n");
		}

		n = heaplib_free_next(n);
	}

	if(!o)
		return heaplib_error_fatal;

	PRINTF("found! node=%p size=%ld \n", o, o->size);

	*vp = (vaddr_t)&o->payload[0];

	/* Now we can safely alter the free list */
	if(heaplib_free_prev(o))
		heaplib_free_next(heaplib_free_prev(o)) = heaplib_free_next(o);
	if(heaplib_free_next(o))
		heaplib_free_prev(heaplib_free_next(o)) = heaplib_free_prev(o);
	if(h->free_list == o)
		h->free_list = heaplib_free_next(o);

	memset(&o->payload[0], 0, o->size);

	o->pc_t.task = GET_PLATFORM_TASKID();
	o->pc_t.flags = f;
	o->pc_t.refs = 1;

	o->magic = HEAPLIB_MAGIC;
	o->active = True;

	h->free -= heaplib_node_size(o);
	h->nodes_active += 1;
	h->nodes_free -= 1;

	return heaplib_error_none;
}

static heaplib_error_t
__heaplib_calloc_try_natural(
	heaplib_region_t * h,
	heaplib_node_t * n,
	heaplib_node_t ** op,
	size_t z,
	heaplib_flags_t f)
{
	if((f & heaplib_flags_natural) != 0)
	{
		return __heaplib_calloc_do_natural(h, n, op, z);
	}

	return __heaplib_calloc_do_split(h, n, op, z);
}

/**
 * \brief Expand or Split a Node for Allocation.
 *
 * \param h [in] The node's region.
 * \param n [in] The node.
 * \param op [out] The allocated node.
 * \param z [in] The size to be allocated.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date January 25, 2020
 */
static heaplib_error_t
__heaplib_calloc_do_split(
	heaplib_region_t * h,
	heaplib_node_t * n,
	heaplib_node_t ** op,
	size_t z)
{
	heaplib_node_t * o;
	size_t x;

	if(heaplib_node_size(n) == z || 
   	  (heaplib_node_size(n) - z) < HEAPLIB_MIN_NODE)
	{
		PRINTF("node expand!\n");
		*op = n;
		return heaplib_error_none;
	}

	PRINTF("node: split! original node size=%ld\n", heaplib_node_size(n));
	x = heaplib_node_size(n);

	/* There's ample room to perform node split */
	n->size = z;
	o = heaplib_node_next(n);

	/* Temporarily add 'b' to the free list so it
 	 * can get adjusted properly later
 	 */
	heaplib_free_next(o) = heaplib_free_next(n);
	heaplib_free_next(n) = o;
	heaplib_free_prev(o) = n;
	if(heaplib_free_next(o))
		heaplib_free_prev(heaplib_free_next(o)) = o;

	o->magic = HEAPLIB_MAGIC;
	o->size = x - z - (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));

	PRINTF("split: new node size=%ld\n", o->size);

	heaplib_footer_init(o);
	heaplib_footer_init(n);

	h->free -= (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));
	h->nodes_free += 1;

	*op = n;

	return heaplib_error_none;
}

/**
 * \brief Evaluate whether we can handle Natural alignment.
 *
 * \param h [in] The node's region.
 * \param n [in] The node.
 * \param op [out] The allocated node.
 * \param z [in] The size to be allocated.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date January 25, 2020
 */
static heaplib_error_t
__heaplib_calloc_do_natural(
	heaplib_region_t * h,
	heaplib_node_t * n,
	heaplib_node_t ** op,
	size_t z)
{
	heaplib_node_t * o;
	vbaddr_t a;
	size_t d;
	size_t m;
	size_t x;

	PRINTF("DO NATURAL node size=%ld\n", heaplib_node_size(n));

	m = z - 1;
	a = (vbaddr_t)((size_t)&n->payload[0] & ~m);

	/* Make sure our base address is within the payload */
	if(a < &n->payload[0])
	{
		a += z;
	}

	/* If we got lucky and the alignment matches payload[0], which is
	 * very unlikely, we just need to split.
	 */
	if(a == &n->payload[0])
	{
		PRINTF("unlikely!!! a == payload[0]\n");
		return __heaplib_calloc_do_split(h, n, op, z);
	}

	do
	{
		/* Make sure we can hit the naturally aligned address
 		 * and that we also have enough room for the chunk.
 		 */
		if(!heaplib_payload_within(n, a, z))
		{
			PRINTF("within failed a=%p z=%ld\n", a, z);
			return heaplib_error_fatal;
		}

		/* Otherwise, we need to split off the space before 'a' as a
	 	 * separate payload if and only if there is enough space for
	 	 * at least the metadata and our minimum chunk size.
	 	 */
		d = a - (vbaddr_t)&n->payload[0];
		if(d >= HEAPLIB_MIN_NODE)
		{
			break;
		}

		/* If 'd' is too small, keep trying until we're out of range */
		a += z;
	}
	while(True);

	/* Factor in the new node header and the base node's footer */
	d -= sizeof(heaplib_node_t);
	d -= sizeof(heaplib_footer_t);

	/* If something weird happened where the node size becomes unaligned
	 * and offset by our word/chunk size, refuse to allocate and warn
	 * that we have a weird number of bytes.
	 */
	if((d % sizeof(size_t)) != 0)
	{
		PRINTF("warning: strange byte alignment!!!\n");
		return heaplib_error_fatal;
	}

	/* Now we know that 'n' represents a viable node so it's OK to edit */
	x = n->size;
	n->size = d;
	n->active = 0;

	/* 'n' is now the prev node */
	/* 'o' is now our natural node */
	o = heaplib_node_next(n);
	o->size = x - (d + sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));
	o->magic = HEAPLIB_MAGIC;
	o->active = 0;

	/* Adjust the metadata */
	heaplib_free_prev(o) = n;
	heaplib_free_next(o) = heaplib_free_next(n);
	if(heaplib_free_next(o))
		heaplib_free_prev(heaplib_free_next(o)) = o;

	heaplib_free_next(n) = o;

	heaplib_footer_init(o);
	heaplib_footer_init(n);

	h->nodes_free += 1;
	h->free -= (sizeof(heaplib_node_t) + sizeof(heaplib_footer_t));

	/* Now that we've got our new naturally aligned node 'o', we can
	 * try and split it, if needs it.
	 */
	return __heaplib_calloc_do_split(h, o, op, z);
}

