#include "platform/platform.h"

/* One chunk is always 32 bytes in size */
#define HEAPLIB_CHUNKSZ (sizeof(size_t))

/* We require a minimum of 4 chunks per node */
#define HEAPLIB_MIN_CHUNKS ((4 * HEAPLIB_CHUNKSZ) +		\
				sizeof(heaplib_node_t) +	\
				sizeof(heaplib_footer_t))

typedef enum heaplib_flags_t heaplib_flags_t;
typedef struct heaplib_node_t heaplib_node_t;
typedef struct heaplib_footer_t heaplib_footer_t;
typedef struct heaplib_region_t heaplib_region_t;

struct
heaplib_region_t
{
	size_t size;
	vaddr_t addr;
	heaplib_flags_t flags;
	heaplib_region_t * next;
};

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
	size_t size;
	size_t magic;

} __attribute__((packed));

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
};

/* Initialization */
extern void heaplib_init(void);

/* Auxiliary */
extern void heaplib_walk(void);

/* Region handling */
extern void heaplib_delete_region(vaddr_t);
extern void heaplib_add_region(vaddr_t, size_t, heaplib_flags_t);

/* Allocation */
extern boolean_t heaplib_free(vaddr_t * );
extern boolean_t heaplib_calloc(vaddr_t *, size_t, size_t, heaplib_flags_t);

