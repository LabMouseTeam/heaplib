#include "platform/platform.h"

#define HEAPLIB_MAGIC 0xDEADF417ULL

/* Minimum conceptual object for alignment */
#define HEAPLIB_CHUNKSZ (sizeof(size_t))
/* Convert chunks to bytes */
#define HEAPLIB_C2B(x) ((x) * HEAPLIB_CHUNKSZ)
/* Convert bytes to chunks (always rounded up) */
#define HEAPLIB_B2C(x) ((x)/HEAPLIB_CHUNKSZ)+((((x)%HEAPLIB_CHUNKSZ)>0)?1:0)

/* We require a minimum of 4 chunks per node */
#define HEAPLIB_MIN_CHUNKS 	8
/* The minimum node size includes the minimum chunks required and the metadata
 * required to drive the free space.
 */
#define HEAPLIB_MIN_NODE ((HEAPLIB_MIN_CHUNKS * HEAPLIB_CHUNKSZ) +	\
				sizeof(heaplib_node_t) +		\
				sizeof(heaplib_footer_t))

typedef size_t heaplib_magic_t;
typedef enum heaplib_flags_t heaplib_flags_t;

typedef enum heaplib_error_t heaplib_error_t;
typedef struct heaplib_node_t heaplib_node_t;
typedef struct heaplib_footer_t heaplib_footer_t;
typedef struct heaplib_region_t heaplib_region_t;
typedef struct heaplib_subregion_t heaplib_subregion_t;

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

	heaplib_flags_smallreq =	(1 << 10), /**< Small requests only */
	heaplib_flags_largereq =	(1 << 11), /**< Large requests only */

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
	heaplib_flags_dontusemask =	(heaplib_flags_restrict |
						heaplib_flags_busy),
};

struct
heaplib_region_t
{
	size_t free;
	size_t size;
	vbaddr_t addr;
	size_t nodes_free;
	size_t nodes_active;
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

	// union {
		struct {
			task_t task;
			union {
				size_t refs:8;
				size_t flags;
			};
		} pc_t;
	// };

	// union {
		struct {
			heaplib_node_t * next;
			heaplib_node_t * prev;
		} free_t;
	// };

	heaplib_magic_t magic;

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

#define heaplib_node_size(x) (((x)->size) & ~1)

#define heaplib_free_next(x) ((x)->free_t.next)
#define heaplib_free_prev(x) ((x)->free_t.prev)

/**
 * \brief Lock a Region or Master according to flags
 *
 * \param x A heaplib Region or Master lock
 * \param f Flags for locking.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 31, 2019
 */
#define heaplib_region_lock_flags(x, f) ({				\
	boolean_t __x;							\
        /* Attempt to lock the Region or Master. Yield to flags */	\
        do {    							\
                __x = heaplib_lock_trylock((x)) == 0;			\
        }								\
        while(!__x && ((f) & heaplib_flags_wait));			\
        (__x == False) ? heaplib_error_again : heaplib_error_none;	\
})

/**
 * \brief Ensure a Node is within the boundaries of a Region
 *
 * \param x A heaplib node 
 * \param y A heaplib Region
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 24, 2019
 */
#define heaplib_region_within(x, y) (((vbaddr_t)(x) >= (y)->addr) && \
					(vbaddr_t)(x) < ((y)->addr + (y)->size))

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
#define heaplib_node_prev(x) ({						\
	heaplib_footer_t * __f;						\
	vbaddr_t __x;							\
	__f = ((heaplib_footer_t * )(((vbaddr_t)(x)) - sizeof(*__f)));	\
	__x = (((vbaddr_t)__f) - heaplib_node_size(__f));		\
	(heaplib_node_t * )(__x - sizeof(heaplib_node_t));		\
})

/**
 * \brief Forward through the heaplib list.
 *
 * \param x A heaplib node.
 *
 * \author Don A. Bailey <donb@labmou.se>
 * \date December 19, 2019
 */
#define heaplib_node_next(x) ((heaplib_node_t * )(			\
				&(x)->payload[				\
					heaplib_node_size((x)) +	\
					sizeof(heaplib_footer_t)]	\
				))

/* Initialization */
extern void heaplib_init(void);

/* Auxiliary */
extern void heaplib_walk(void);

/* Region handling */
__attribute__((always_inline)) __inline__ heaplib_error_t
heaplib_region_trylock(heaplib_region_t * h, boolean_t w) 
{
	do {
		/* Platform trylock policy is to always return boolean */
		if(heaplib_lock_trylock(&h->lock) == 0)
			return heaplib_error_none;
	}
	while(w);

	/* Always return Again if we can't lock immediately. */
	return heaplib_error_again;
}

#define HEAPLIB_REQUEST_THRESHOLD(x) ((x)->size / 16)

__attribute__((always_inline)) __inline__ boolean_t
__validate_region_request(heaplib_region_t * h, size_t z)
{
	if((h->flags & (heaplib_flags_smallreq|heaplib_flags_largereq)) == 0)
	{
		return True;
	}

	if((h->flags & heaplib_flags_smallreq) && z < HEAPLIB_REQUEST_THRESHOLD(h))
	{
		return True;
	}

	return ((h->flags & heaplib_flags_largereq) && z >= HEAPLIB_REQUEST_THRESHOLD(h)) || 
		((h->flags & heaplib_flags_largereq) && z < HEAPLIB_REQUEST_THRESHOLD(h));
}

/* Region handling */
extern heaplib_error_t heaplib_region_delete(heaplib_region_t * );
extern heaplib_error_t heaplib_region_add(vaddr_t, size_t, heaplib_flags_t);
extern heaplib_error_t heaplib_region_find_next(heaplib_region_t **, heaplib_flags_t);
extern heaplib_error_t heaplib_region_find_first(heaplib_region_t **, heaplib_flags_t);
extern heaplib_error_t heaplib_ptr2region(vaddr_t, heaplib_region_t **, heaplib_flags_t);

/* Allocation */
extern heaplib_error_t heaplib_free(vaddr_t *, heaplib_flags_t);
extern heaplib_error_t heaplib_calloc(vaddr_t *, size_t, size_t, heaplib_flags_t);

/* Pointer to Node conversion */
extern boolean_t heaplib_ptr2node(heaplib_region_t *, vaddr_t, heaplib_node_t ** );

/* Debug */
extern void heaplib_walk(void);

