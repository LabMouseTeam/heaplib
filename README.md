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
 - first stage: enable region deletion
	- this will ensure that we know *when* we can delete a region
	- this requires tracking whether pointers are allocated




