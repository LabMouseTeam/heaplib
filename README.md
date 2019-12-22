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


# ############################################################################
#
# Region Support
#

# _XXX TO DO XXX_
#
 - add lockable subregions of size N, within Regions 
	- this means that multiple threads can work on the same Region at the same time
	  as long as they are calloc and free'ing different subregions
	- which is pretty rad because it increases parallelism
		- should be minimal expense to add this feature


# Region Generics
#
 - using a Region Array removes transitive locking (pointers infer transitive locking)
 - we can use the Master Lock to add/del/alter Regions without worry or deadlock
 - we can use the Region-specific lock to alloc/free within a Region without locking other Regions
	- this way we can alloc in multiple threads without much (or any) delay
 - no potential for deadlock on trying to move across Regions

# Example
#
 - thread locks Master to acquire Region
 - Region is returned Locked
 - another thread holds the adjacent Region locked
 - Thread attempts to go to the next Region and can't becuse it is locked
 - Thread either waits for lock or fails immediately (if nowait is set)
 - this is OK because
	- we can sit on the Locked region since we know it will still be valid once the current consumer releases it
	- we know the new Region will still be valid because the Master Lock was not held
		- so the Region cannot be altered if the Master Lock is not held

# Policies
#
 - deadlock scenario #1
	- Thread Alpha (A) holds Region 1's Lock
	- Thread Beta (B) holds Region 2's Lock
	- 'A' wants to move to Region 2
	- 'B' wants to move to Region 3
	- 'A' attempts to lock 2 first, which holds both the Master and 2's Lock
		- 'A' then is in a Wait state for 'B' to unlock 2
	- 'B' then requests to move to 3, attempts to take the Master Lock, but must wait
	- we now have deadlock
		- This is because 'B' will wait forever for 'A' to release the Master
		- but in order for 'A' to release the master, 'B' must release 2, which will never happen
	+ The solution to this is to unlock the Region prior to acquiring the Master 
		- this way, the Region can be used by another caller 
		! one Gotcha here is that the 'current' Region can disappear once we release it, so we can't
		  rely on its information to move forward
			- so the 'next' function must take the base address of the current Region and the Flags from the caller
			- then unlock the Region
			- using this method, we won't get deadlock and have the info we need to move on

 - policy #1
	- all Regions must be unlocked by heaplib when requesting the 'next' region
	- this ensures we evade deadlock scenario #1

 - policy #2
	- a Region can only be added, deleted, or its attributes altered when both the Master lock and its Region lock are held

 - policy #3
	- to find a First Region
		- lock the master lock so we can walk the Region table
		- for each Region
			- lock the region
			- verify that Flags match
			- verify the Free size matches
			- if no match, goto next
		- on match
			- unlock master
				- this just makes more sense and is faster than unlocking every time we search a Region node
			- return locked region

 - policy #4
	- to find a Next Region
		- get the Flags from function args
		- retrieve the current Region's base address
		- unlock the Region
		- lock the Master
		- for each potential Next Region
			- lock it
			- verify the Flags match
			- verify the Free size matches
		- unlock Master
		- on match
			- return locked region

 - policy #5
	- release a region
	- simply unlock it


# calloc process with Region support
#
#
 - lock the Master Region Lock
 x check the total_free to see if we even have enough free bytes
	x this is useless because of Region support
 - once locked, we can evaluate each Region to determine if we can support the request
	- first check if the Requested Flags match
	- next, check if the Free Size >= Request Size
 - if it DOESNT match, we need to get the Next Region
	- we give the current region back to the Region library (still locked)
	- Region Library orders the Regions based on starting address address (of the actual Region base addr, not the &Region)
	- Region Lib will fetch the Next Region and attempt to Lock it within the context of the current Thread
		- Lock Next Region or Nil (do nothing)
		- Unlock Current Region
		- Return Next Region or Nil


# REQUIREMENT
#
 - 

