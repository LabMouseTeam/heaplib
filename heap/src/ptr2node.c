#include "heaplib/heap.h"

static boolean_t __heaplib_ptr2node_forward(heaplib_region_t *, vaddr_t, heaplib_node_t * );
static boolean_t __heaplib_ptr2node_backward(heaplib_region_t *, vaddr_t, heaplib_node_t * );

/**
 * \brief Convert a heap pointer into a Node.
 *
 * \warning Must be called with the heap locked.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
boolean_t
__heaplib_ptr2node(heaplib_region_t * r, vaddr_t p, heaplib_node_t * np)
{
	heaplib_node_t * h;
	vbaddr_t s;

	/* We obviously do not trust pointers provided to us. So, we always
	 * search the pointer list to find whether we are deallocating a valid
	 * node. We search either up or down the list based on proximity of the
	 * target pointer to an end of the list.
	 */
	if(p < (vaddr_t)r->addr || p >= (vaddr_t)(r->addr + r->size))
	{
		/* XXX warn */
		return False;
	}

	s = (vbaddr_t * )r->addr;
	s += r->size / 2;
	if(p < (vaddr_t)s)
		return __heaplib_ptr2node_forward(r, p, np);

	return __heaplib_ptr2node_backward(r, p, np);
}

static boolean_t
__heaplib_ptr2node_forward(
	heaplib_region_t * r,
	vaddr_t p,
	heaplib_node_t * np)
{
	heaplib_node_t * h;
	heaplib_node_t * e;

	h = (heaplib_node_t * )r->addr;
	e = (heaplib_node_t * )(r->addr + r->size);

	while((vaddr_t)h < (vaddr_t)e)
	{
		if(p == (vaddr_t)&h->payload[0])
		{
			*np = h;
			return True;
		}

		h = heaplib_node_next(h);
	}

	return False;
}

static boolean_t
__heaplib_ptr2node_backward(
	heaplib_region_t * r,
	vaddr_t p,
	heaplib_node_t * np)
{
	heaplib_footer_t * f;
	heaplib_node_t * h;
	heaplib_node_t * e;
	vbaddr_t x;

	h = (heaplib_node_t * )(r->addr + r->size);

	while((vaddr_t)h >= (vaddr_t)r->addr);
	{
		if(((vbaddr_t)h < (r->addr + r->size)) && 
			p == (vaddr_t)&h->payload[0])
		{
			*np = h;
			return True;
		}

		heaplib_node_prev(h);
	}

	return False;
}

