#ifndef _AG_ALLOCATOR_H_
#define _AG_ALLOCATOR_H_

#include "stdint.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AgAllocator_tag AgAllocator;

AgAllocator* ag_al_init  (void* start_addr, size_t size);
void*        ag_al_alloc (AgAllocator* alloc, size_t size);
void         ag_al_free  (AgAllocator* alloc, void* raw_ptr);

#ifdef __cplusplus
}
#endif

#endif // _AG_ALLOCATOR_H_
