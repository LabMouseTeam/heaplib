# heaplib
A Heap Library for both Kernel and Userland Use

# Nomadic Support
#

# goals
 - enable the movement of allocated nodes to a new region
 - *when* is immaterial right now
	- one scenario may be that we ask a task to migrate memory that can help resolve an allocation that would otherwise result in an OOM condition
	- another scenario is that the app is migrating to a new environment (another Lab Mouse module) and needs to migrate across a Cloud Bus
	- another scenario is that we've added a memory region over CloudBus and want to migrate apps to it

 - in order to make this work
	- we have to know how to tell a kthread to migrate
	- unprivileged apps ("userland") should be force-migratable without them having to assist
		- only exception is "secure" userland apps that cannot migrate to "non-secure" regions
			- these apps should not migrate themselves we just need to place them safely

 - we can migrate nomadic memory nodes in one of two ways
	- on a per-node basis
		- this implies we know how to go from node -> task, and can use a task callback to request migration
	- on a per-task/app basis
		- this implies we monitor each task/app's memory usage
		- can determine if an app is using too much memory (or what percentage)
		- can migrate an app away if it demanding too many resources
		- we determine whehter an app needs to be migrated (can be for any reason)
			- and we migrate it 

# implementation
#
 x COMPLETE: first stage: enable region deletion
	- this will ensure that we know *when* we can delete a region
	- this requires tracking whether pointers are allocated
		- turns out all we need to do is identify whether nodes are active

 - nomadic pages
	- alloc side
		- thread requests alloc
		- alloc would cause OOM condition
		- allocator notes region has nomadic entries
		- alloc determines if nomadic pages are adjacent to free nodes
			- if yes
			- AND if combined there is enough space for the request
			- determine if there is a place to migrate the nomadic page(s) to to fulfill the request
			- if there is
				- SET EACH DESTINATION NODE as heaplib_flags_busy to mark them as a target for migration
					- DONT FORGET to adjust the node_free/node_active and free stats
					- DONT FORGET to alter the allocator to check for the busy flag and skip busy nodes
				- SET EACH NOMADIC SOURCE NODE to heaplib_flags_busy to note that a free() should lock the node for the pending alloc() by the requesting thread
					- otherwise the free() operation executed in the migrating thread(s) may free up a node that could be consumed by another thread allocating
					  in the window between the free() and when the requesting thread wakes up
				- success

		! ENSURE the requesting thread is NOT THE SAME as the migrating threads
			- otherwise, might deadlock

		- next, set the requesting thread to sleep() waiting on completion (either success or not)

		- now, request that the thread(s) owning the nomadic page(s) perform migration
			- set pending MEMORY_MIGRATE event for each thread
				- set source address
				- set destination address
			- this is NOT a callback, it is an event that must be processed by the thread so it can properly handle memory management
				! if the thread is interrupted, we may interrupt a use of the very addresses we are changing
				! an event is better because the target thread can handle it

			- once threads migrate the pages they call free() on the migrated node

		- now, the free() operation actually completes the calloc() for the requesting thread by waking up the requesting thread sleeping in calloc()
			- the free() re-allocates the nodes
			- the free() unlocks (wakes up) the sleeping thread
			- the sleeping thread, waiting in calloc(), wakes up
				- coalesces the now free adjacent nodes
					- a coalesce is performed to merge the N adjacent nodes, busy flag is ignored because they are !active
				- once coalesced, the joined busy node is allocated to the requesting thread
				- the calloc() now returns 
					- requesting thread is woken up with the success/fail of the operation


