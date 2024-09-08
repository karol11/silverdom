#include <stdint.h>
#include "allocator.h"

// Heap supports up to 2^48 addresses
// It resides in up to 2^32 64k-blocks between start_addr and end_addr
// Each 64K block starts with a 64-bit header, so it holds effectively 65536-8 bytes
// Header stores an `allocated` flag. If block is not allocated,
// its first 2 64-bit words are used to link this block int an itrusive list of free blocks.
// 64k blocks are organized in a pyramid by 2^n sizes.
// For each size it has a list of free blocks of this size.
// Every time a block or size N>=32K is allocated it uses the closest 2^x block size.
// If there is no block of size 2^x, it takes a block 2^(x+1) and splits it in two of size 2^x.
// One of these blocks is returned to a 2^x list, other one gets returned as the allocation result.
// If there is no block of size 2^(x+1) it recursively splits block of size 2^(x+2) and so on.
// Upon dealloction, it checks only for its pair (buddy) block with which it was previously split,
// and if this block is vacant, it combines these blocks. Such defragmentation is performed
// upwards by all levels up to 2^32.
// Requests of sizes <= 2^16 get fulfilled from 64K pages
// organized in 64 pools - one pool for one fixed size.
// Each pool has its own free list.
// If a pool empty, it pool allocates another 64K page from buddy allocator and slice it in items.
// Pools never defragmented and never returned to buddy allocator.
// (todo: return pages with fully deallocated items).
// If heap is created in the persistent memory, it should be initialized only once upon first creation.
// Heap is not thread-safe, serialize ag_al_alloc/ag_al_free calls.
// Heap needs at least 4Mb memory to allocate all possible pools.
// It is optimized for speed, locality, large number of allocated blocks and little fragmentation.
// It is aimed at allocating small blocks in a huge address space. 

#define AG_AL_F_BUDDY 2
#define AG_AL_F_ALLOCATED 1
#define AG_AL_F_OFFSET 2
#define AG_AL_BUDDY_COUNT 32
#define AG_AL_PAGE_COUNT 64

// #define AG_AL_TRACE
#ifdef AG_AL_TRACE
#include <stdio.h>
#define AG_TRACE(...) printf(__VA_ARGS__)
#else
#define AG_TRACE(...)
#endif

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanReverse)
#endif

inline size_t bytes_to_width(size_t size) {
#ifdef _MSC_VER
    uint32_t r = 0;
    _BitScanReverse64(&r, size);
    return r;
#elif defined(__GNUC__) || defined(__clang__)
    return 64 - __builtin_clzll(size - 1);
#else
#error "Unsupported compiler"
#endif 
}

static size_t bytes_to_buddy(size_t size) {
    return size <= 65536 ? 0 : (bytes_to_width(size - 1) - 15);
}

inline size_t buddy_to_bytes(size_t buddy) {
    return 1llu << (buddy + 16);
}

static size_t bytes_to_page(size_t size) {
    if (size <= 96) return (size + 31) / 32 - 1;
    if (size <= 1920) return (size - 128 + 63) / 64 + 3;
    return 65 - 8192 / ((size + 7) / 8);
}

static size_t page_to_bytes(size_t page) {
    if (page <= 2) return 32 * (page + 1);
    if (page < 32) return 64 * (page - 3) + 128;
    return 8192 / (63 - page + 2) * 8;
}

typedef struct AgAlListItem_tag AgAlListItem;

typedef struct AgAlListItem_tag {
    AgAlListItem *next;
    AgAlListItem *prev;
} AgAlListItem;

inline void link_to_list(AgAlListItem* list, AgAlListItem* item) {
    item->next = list->next;
    item->prev = list;
    item->next->prev = item->prev->next = item;
}

inline void unlink_from_list(AgAlListItem* item) {
    item->next->prev = item->prev;
    item->prev->next = item->next;
}

inline int is_empty(AgAlListItem* i) {
    return i->next == i;
}

typedef struct { // this header stores info of buddy block and every page item
    uint64_t header; // AG_AL_F_* flags | page_size_index 0..63 or buddy_size_index 0..32
    AgAlListItem free_list;  // this field exists only in free items.
} AgAlHeader;

typedef struct AgAllocator_tag {
    void *end;
    AgAlListItem b_free[AG_AL_BUDDY_COUNT]; // roots of double-linked lists of buddy blocks of sizes 32769+..2^48
    AgAlListItem p_free[AG_AL_PAGE_COUNT]; // roots of double-linked lists of free page items of sizes 32..32768
} AgAllocator;

// Called one time when heap is created in a raw memory.
AgAllocator* ag_al_init(void* start_addr, void* end_addr) {
    AgAllocator* me = (AgAllocator*) start_addr;
    me->end = end_addr;
    for (AgAlListItem *i = me->b_free, *n = i + AG_AL_BUDDY_COUNT; i < n; i++)
        i->next = i->prev = i;
    for (AgAlListItem *i = me->p_free, *n = i + AG_AL_PAGE_COUNT; i < n; i++)
        i->next = i->prev = i;
    char* start = (char*) (me + 1);
    size_t total_bytes = (char*)end_addr - start;
    size_t buddy = bytes_to_buddy(total_bytes) - 1;
    size_t step = buddy_to_bytes(buddy);
    for (; step >= 65536; buddy--, step >>= 1) {
        for (; (char*)end_addr - start >= step; start += step) {
            AG_TRACE("Inited block @%lld of size %lld\n", ((char*)start - (char*)(me + 1)) / 65536, step / 65536);
            AgAlHeader* h = (AgAlHeader*) start;
            h->header = buddy << AG_AL_F_OFFSET | AG_AL_F_BUDDY;
            link_to_list(&me->b_free[buddy], &h->free_list);
        }
    }
    return me;
}

void* ag_al_alloc(AgAllocator* alloc, size_t size) {
    AG_TRACE("Alloc @%lld bytes\n", size);
    size = (size + 8 + 7) & ~3;
    size_t page = size <= 32768 ? bytes_to_page(size) : 0;
    if (size <= 32768 && !is_empty(&alloc->p_free[page])) {
        AgAlListItem* r = alloc->p_free[page].next;
        AG_TRACE("use page %lld from block @%lld\n", ((((char*)r - (char*)(alloc + 1)) - 8) % 65536) / page_to_bytes(page), (((char*)r - (char*)(alloc + 1)) - 8) / 65536);
        unlink_from_list(r);
        ((AgAlHeader*)((uint64_t*)r - 1))->header |= AG_AL_F_ALLOCATED;
        return r;
    }
    size_t buddy = size > 65536 ? bytes_to_buddy(size) : 0;
    size_t upper = buddy;
    while(is_empty(&alloc->b_free[upper]))
        upper++;
    AgAlListItem* r = alloc->b_free[upper].next;
    unlink_from_list(r);
    AG_TRACE("grab block @%lld of size %lld\n", (((char*)r - (char*)(alloc + 1)) - 8) / 65536, buddy_to_bytes(upper) / 65536);
    while (upper > buddy) {
        link_to_list(&alloc->b_free[--upper], r);
        AG_TRACE("split block @%lld to size %lld\n", (((char*)r - (char*)(alloc + 1)) - 8) / 65536, buddy_to_bytes(upper) / 65536);
        ((int64_t*)r)[-1] = upper << AG_AL_F_OFFSET | AG_AL_F_BUDDY;
        ((char*)r) += buddy_to_bytes(upper);
    }
    if (size >= 32768) {
        AG_TRACE("return block @%lld of size %lld\n", (((char*)r - (char*)(alloc + 1)) - 8) / 65536, buddy_to_bytes(buddy) / 65536);
        ((int64_t*)r)[-1] = buddy << AG_AL_F_OFFSET | AG_AL_F_BUDDY | AG_AL_F_ALLOCATED;
        return r;
    }
    size_t page_size = page_to_bytes(page);
    AgAlListItem* i = (AgAlListItem*)((char*)r + page_size);
    AgAlListItem* n = (AgAlListItem*)((char*)r + 65536 - page_size);
    AG_TRACE("use page 0 from block @%lld sliced into pages of size %lld\n", (((char*)r - (char*)(alloc + 1)) - 8) / 65536, page_size);
    for (; i <= n; ((char*)i) += page_size) {
        ((uint64_t*)i)[-1] = page << AG_AL_F_OFFSET;
        link_to_list(&alloc->p_free[page], i);
    }
    ((uint64_t*)r)[-1] = page << AG_AL_F_OFFSET | AG_AL_F_ALLOCATED;
    return r;
}

void ag_al_free(AgAllocator* alloc, void* raw_ptr) {
    AgAlHeader* ptr = (AgAlHeader*) ((uint64_t*)raw_ptr - 1);
    ptr->header &= ~AG_AL_F_ALLOCATED;
    if (ptr->header & AG_AL_F_BUDDY) {
        for (;;) {
            AgAlHeader* buddy_addr = (AgAlHeader*) (
                (char*)(alloc + 1) + (
                    (((char*)ptr - (char*)(alloc+1)) ^
                    (1llu << ((ptr->header >> AG_AL_F_OFFSET) + 16)))));
            if ((char*)buddy_addr < (char*)(alloc+1) || 
                (char*)buddy_addr > (char*)(alloc->end) ||
                buddy_addr->header != ptr->header)
                break;
            unlink_from_list(&buddy_addr->free_list);
            if (buddy_addr < ptr)
                ptr = buddy_addr;
            ptr->header = ((ptr->header >> AG_AL_F_OFFSET) + 1) | AG_AL_F_BUDDY;
        }
        link_to_list(
            &alloc->b_free[ptr->header >> AG_AL_F_OFFSET],
            &ptr->free_list);
    } else {
        link_to_list(
            &alloc->p_free[ptr->header >> AG_AL_F_OFFSET],
            &ptr->free_list);
    }
}
