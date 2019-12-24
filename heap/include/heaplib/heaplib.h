#include "platform/platform.h"

#define HEAPLIB_MAGIC 0xDEADF417ULL

/* Minimum conceptual object for alignment */
#define HEAPLIB_CHUNKSZ (sizeof(size_t))
/* Convert chunks to bytes */
#define HEAPLIB_C2B(x) ((x) * HEAPLIB_CHUNKSZ)
/* Convert bytes to chunks (always rounded up) */
#define HEAPLIB_B2C(x) ((x)/HEAPLIB_CHUNKSZ)+((((x)%HEAPLIB_CHUNKSZ)>0)?1:0)

/* We require a minimum of 4 chunks per node */
#define HEAPLIB_MIN_CHUNKS 	4
/* The minimum node size includes the minimum chunks required and the metadata
 * required to drive the free space.
 */
#define HEAPLIB_MIN_NODE ((HEAPLIB_MIN_CHUNKS * HEAPLIB_CHUNKSZ) +	\
				sizeof(heaplib_node_t) +		\
				sizeof(heaplib_footer_t))

typedef size_t heaplib_magic_t;
typedef enum heaplib_flags_t heaplib_flags_t;

typedef struct heaplib_node_t heaplib_node_t;
typedef struct heaplib_footer_t heaplib_footer_t;
typedef struct heaplib_region_t heaplib_region_t;
typedef struct heaplib_subregion_t heaplib_subregion_t;

struct
heaplib_region_t
{
	size_t id;
	size_t free;
	size_t size;
	vbaddr_t addr;
	heaplib_lock_t lock;
	heaplib_flags_t flags;
	heaplib_node_t * free_list;

} __attribute__((packed));

struct
heaplib_node_t
{
	union {
		size_t active:1;
		size_t size;
	};

	union {
		struct {
			task_t task;
			union {
				size_t refs:8;
				size_t region:8;
				size_t flags;
			};
		} pc_t;

		struct {
			heaplib_node_t * next;
			heaplib_node_t * prev;
		} free_t;
	};

	uint8_t payload[];

} __attribute__((packed));

struct
heaplib_footer_t
{
	heaplib_magic_t magic;
	size_t size;

} __attribute__((packed));

struct
heaplib_subregion_t
{
	heaplib_magic_t magic;
	heaplib_lock_t lock;
	size_t size;

} __attribute__((packed));

enum
heaplib_error_t
{
	heaplib_error_none =		0,	/* All good */
	heaplib_error_fatal =		1,	/* Operation failure */
	heaplib_error_again =		2,	/* Try again later */
};

enum
heaplib_flags_t
{
	heaplib_flags_internal =	(1 << 0), /**< Internal SRAM Only */
	heaplib_flags_nomadic =		(1 << 1), /**< Can migrate */
	heaplib_flags_wait =		(1 << 2), /**< Always wait */
	heaplib_flags_nowait =		(1 << 3), /**< Don't wait */
	heaplib_flags_busy =		(1 << 4), /**< Pending an update */
	heaplib_flags_restrict =	(1 << 5), /**< No further use */
	heaplib_flags_encrypted =	(1 << 6), /**< Encrypted bus */
	heaplib_flags_active =		(1 << 7), /**< Region is Active */
	heaplib_flags_wiped =		(1 << 8), /**< Zero on free */
	heaplib_flags_subregions =	(1 << 9), /**< Contains subregions */

	/* Flags for defining a Region */
	heaplib_flags_regionmask =	(heaplib_flags_wiped |
						heaplib_flags_internal |
						heaplib_flags_encrypted),

	/* Flags specific to a Node */
	heaplib_flags_nodemask =	(heaplib_flags_nomadic |
						heaplib_flags_busy |
						heaplib_flags_wiped |
						heaplib_flags_restrict),

	/* Security related flags */
	heaplib_flags_securitymask =	(heaplib_flags_wiped |
						heaplib_flags_internal |
						heaplib_flags_encrypted),

	/* "Don't use" flags */
	heaplib_flag_dontuse =		(heaplib_flags_restrict |
						heaplib_flags_busy),
};

#define heaplib_region_next(x) ((x)->next)
__inline__ heaplib_error_t
heaplib_region_safenext(
	heaplib_region_t * h,
	heaplib_region_t ** op,
	heaplib_flags_t f)
{
	heaplib_region_t * n;

	/* The current Region is always locked at this point. To safely
	 * get to the next usable and active node, we can peek at the
	 * next node, which is always safe to inspect. Locking the current
	 * node means we put the next node (if it exists) in check until
	 * we are unlocked. This transitively checks the second degree node
	 * to our Next, if the first degree is Locked.
	 */

	/* 1) Is the next Region valid
	 * 2) Is the Region Restricted|Busy
	 * 3) Is the Region Lockable
	 * 4) Lock and Return
	 */
	if(!h->next)
		return heaplib_error_fatal;
	h = h->next;

	e = heaplib_region_trylock(h, f);
	if(e == heaplib_error_none)
	{
		if(!(h->flags & (heaplib_region_busy|heaplib_region_restrict)))
		{
			/* We safely own this Region */
			*op = h;
			return heaplib_error_none;
		}

		/* This region is unstable. Inspecting next is safe */
		if(!h->next)
		{
			heaplib_unlock(&h->lock);
			return heaplib_error_fatal;
		}

		n = h->next;
	}
	else
	{
		/* We can't wait for this locked region, because we aren't
		 * waiting at all if we reach this branch. So skip ahead to
		 * the next node and attempt to acquire it. If we wait for
		 * this node, we'll wait forever because of the transitive
		 * lock. So, we can't even check this node's flags because
		 * we can't rely on them until we own the Lock. Thus, our 
		 * only option is to fast forward.
		 */
		n = 
	}

} __attribute__((always_inline));

#define heaplib_node_size(x) (((x)->size) & ~1)

#define heaplib_free_next(x) ((x)->free_t.next)
#define heaplib_free_prev(x) ((x)->free_t.prev)

/**
 * \brief Return the footer of a node.
 *
 * \param x A heaplib node or the end of the heaplib region.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 23, 2019
 */
#define heaplib_node_footer(x) 						\
	(heaplib_footer_t * )(&(x)->payload[				\
		heaplib_node_size((x))]);

/**
 * \brief Rewind through the previous heaplib list.
 *
 * \param x A heaplib node or the end of the heaplib region.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
#define heaplib_node_prev(x) do {					\
	heaplib_footer_t * __f;						\
	vbaddr_t __x;							\
	__f = ((heaplib_footer_t * )(((vbaddr_t)(x)) - sizeof(*__f)));	\
	__x = (((vbaddr_t)__f) - heaplib_node_size(__f));		\
	(x) = (heaplib_node_t * )(__x - sizeof(heaplib_node_t));	\
} while(0);

/**
 * \brief Forward through the heaplib list.
 *
 * \param x A heaplib node.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
#define heaplib_node_next(x) ((heaplib_node_t * )&(			\
				(x)->payload[				\
					heaplib_node_size(x) +		\
					sizeof(heaplib_footer_t)]	\
				))

/* Initialization */
extern void heaplib_init(void);

/* Auxiliary */
extern void heaplib_walk(void);

/* Region handling */
__inline__ heaplib_error_t
heaplib_region_trylock(heaplib_region_t * h, boolean_t w)
{
	do {
		/* Platform trylock policy is to always return boolean */
		if(heaplib_trylock(&h->lock) == 0)
			return heaplib_error_none;
	}
	while(w);

	/* Always return Again if we can't lock immediately. */
	return heaplib_error_again;
} __attribute__((always_inline));

/* Region handling */
extern heaplib_error_t heaplib_region_delete(heaplib_region_t);
extern heaplib_error_t heaplib_region_add(vaddr_t, size_t, heaplib_flags_t);
extern heaplib_error_t heaplib_region_find_next(heaplib_region_t **, heaplib_flags_t);
extern heaplib_error_t heaplib_region_find_first(heaplib_region_t **, heaplib_flags_t);

/* Allocation */
extern heaplib_error_t heaplib_free(vaddr_t * );
extern heaplib_error_t heaplib_calloc(vaddr_t *, size_t, size_t, heaplib_flags_t);

/* Pointer to Node conversion */
extern boolean_t __heaplib_ptr2node(
			heaplib_region_t *,
			vaddr_t,
			heaplib_node_t * );

/* Debug */
extern void heaplib_walk(void);

