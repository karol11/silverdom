#include "allocator.h"

static char heap[2 * 1024 * 1024];
static AgAllocator* allocator;

inline void* malloc(size_t size) { return ag_al_alloc(allocator, size); }
inline void free(void* ptr) { ag_al_free(allocator, ptr); }

int main() {
    allocator = ag_al_init(heap, heap + sizeof(heap));
    char* m1 = (char*)malloc(100);
    free(m1);
}
