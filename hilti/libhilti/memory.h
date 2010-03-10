// $Id$
// 
// Replacement functions for malloc/calloc/realloc that provide automatic
// garbage collection.

#ifndef HILTI_MEMORY_H
#define HILTI_MEMORY_H

#include <stdint.h>

// Allocates n bytes of memory and promises that they will never contain any
// pointers.
extern void* hlt_gc_malloc_atomic(uint64_t n);

// Allocates n bytes of memory, which may be used to store pointers to other
// objects. 
// 
// Todo: At some point, we'll likely change this interface to require a
// pointer map to be passed in.
extern void* hlt_gc_malloc_non_atomic(uint64_t n);

// Allocates n bytes of zero-initialized memory and promises that they will
// never contain any pointers.
extern void* hlt_gc_calloc_atomic(uint64_t count, uint64_t n);

// Allocates n bytes of zero-initialized memory, which may be used to store
// pointers to other objects. 
// 
// Todo: At some point, we'll likely change this interface to require a
// pointer map to be passed in.
extern void* hlt_gc_calloc_non_atomic(uint64_t count, uint64_t n);

// Reallocates the memory to a chunk of size n and promises that they never
// contain any pointers. The original memory must have been allocated with a
// *_atomic function as well.
extern void* hlt_gc_realloc_atomic(void* ptr, uint64_t n);

// Reallocates the memory to a chunk of size n, which may be used to store
// pointers to other objects. The original memory must have been allocated
// with a *_non_atomic function as well.
extern void* hlt_gc_realloc_non_atomic(void* ptr, uint64_t n);

// Initializes the GC subsystem. This is called from hilti_init().
void __hlt_init_gc();

#endif
