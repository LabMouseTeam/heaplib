# heaplib-public
The Lab Mouse Inc. heap memory implementation.

# Initialization
Before performing allocations or region setup, simply call init:
```C
heaplib_init();
```

This will initialize all platform-specific region locks and the global lock.

# Configuration
Before allocation can occur, heaplib needs to know about the regions of
physical or virtual memory that it will control. This can be done by
adding a region.
```C
heaplib_error_t r;
r = heaplib_region_add(SRAM_BASE, SRAM_SIZE, heaplib_flags_internal);
```

# Allocation
In Lab Mouse heaplib, there is no malloc or realloc. There is only calloc,
which guarantees that memory has been "cleaned" to zero prior to return. If
your memory region needs to grow, simply call calloc again prior to calling
free on the previous chunk. A lack of realloc may change in the future.

Allocate heap memory in the traditional fashion, with a slight variance in how
the function is called. 
```C
heaplib_error_t r;
char * x;
r = heaplib_calloc(&x, 1, 8, heaplib_flags_wait);
if(r != heaplib_error_none)
{
	printf("error: allocation failed.\n");
	return r;
}
...
```

# Free
Freeing data is simple, and the free function always ensures that no dangling
pointers are left, by setting the address to nil. This should always be a
valid operation, as there should be no case where a heap address would be used
after the pointer were free'd.
```C
heaplib_free(&x, 0);
```

# Coalesce
While heaplib is a lazy allocator, it does attempt to coalesce heap nodes at
opportunitistic points. This occurs when the heap is perceived as fragmented,
or when an OOM condition would occur when bytes-free is sufficient to fulfill
the request. 

# Nomadic Chunks
In a future version, heaplib will support *nomadic* memory.

# Notable Flags
Flags can be used to ensure allocation only occurs in a region with a matching
configuration. For example, if an internal region of static RAM were required
for security/privacy reasons, the caller can pass the flag 
*heaplib_flags_internal*. Allocation will only occur if heaplib can find a
sufficient node of memory within a region that is set as *internal*. This is
also true of *heaplib_flags_encrypted*, which denotes a region where encrypted
RAM is available for security purposes.

Another critical flag is *heaplib_flags_natural*. This alignment flag will
cause the allocator to attempt allocation on a natural (logarithmic) address
per the request size. The allocator will split memory, where possible, to
ensure the *payload address* adheres to natural alignment. If natural
alignment is not possible, heaplib will return failure. It should be noted
that a failure indication when attempting naturally aligned allocation is
*not* necessarily an indicator of an out-of-memory condition.

