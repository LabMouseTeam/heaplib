# heaplib
A Heap Library for both Kernel and Userland Use

# Method (testing)
#
 - initialize
	- MUST occur before multiproc/scheduler is running
	- set up all the things relevant to memory allocation
	- locks and such
	- but don't set up the actual memory region(s) here

 - add_region
	- lock mem_lock
		- defer unlock mem_lock
	- base address of the region
	- size of the region
	- flags for the region (secure, non-secure, etc)

 - calloc (malloc is not supported)
	- lock mem_lock
		- defer unlock mem_lock
	- convert size request into chunks, round up if necessary, then convert back to bytes
	- determine if our tracked free bytes value supports this
		- if not, return nil
	- we believe we have enough free memory now, so try and run an internal internal_calloc
		- use FLAGS to determine what region of memory to fulfill the request
	- if OK, return ptr
	- on error, if FLAGS_NOWAIT
		- return False
	- otherwise, we are ok to continue
	- try
		- coalesce
		- [migrate]
		- internal_calloc
		- while FLAGS_WAIT is Set

 - internal_calloc
	- lock
		- defer unlock
	- loop through the free list
		- if the Node Size >= request AND the Node is NOT busy (free nodes can be busy with other cross-proc ops)
			- if the request is equal to the node size OR the excess free space is TOO SMALL to split
				- THIS CHECK requires (MIN_FREE_SPACE + HEADER/FOOTER EXTRAS)
					- headers MUST Be calculated here because of shrinkage in the split
						- otherwise result of the split will have a Node with sub-MINCHUNK bytes
				- just use This Node because it's either theright fit or we give the caller a bit extra
			- otherwise, SPLIT
				- only allocate REQUEST bytes in NODE (because we've already rounded up)
				- create a new Footer at the end of the current Node
				- create a Header immediately after the Node
				- adjust the original Footer to reflect the new smaller chunk size
				- the new Free node shall be placed on the free list in the appropriate location
				- adjust total_free to include the new free bytes MINUS the Header/Footer
			- PREPARE the new Node for return
				- set to ACTIVE
				- set the Task
				- set the Flags
				- set the refs to 1
				- reduce the total_free 
				- RETURN the PAYLOAD POINTER
		- if we got here, we didn't find anything of value

 - free
	- lock
		- defer unlock
	- ensure the pointer is within a valid region range
	- find the region that fits the pointer or fail
	- once the region is found
		- determine if the address is closer to the start or the end of the region
		- if start, search forward from the region base address
		- if end, search backward from the region end
		- if we exceed the target address and the node is NOT found the free() failed due to bad pointer
			- return error
	- here we know the pointer is valid and the node has been found
	- ensure the node is being freed by the same task that owns it
		- if not, error; a task must only free its own allocations
	- if SECURE, clean the chunk prior to putting the memory back on the free list
		- if Non Secure, the consumer of the allocation is required to clean its own data prior to free()'ing
	- mark the node as NOT ACTIVE
	- find the closest Free node earlier in memory
		- if none exists, check hfree
			- if hfree is NOT nil, prev becomes hfree
	- if earlier node
		- link earlier node's next to This Node
		- link earlier node's old next's prev to This Node
	- if NO earlier node
		- set hfree to This Node
	- increase the Total Free count


