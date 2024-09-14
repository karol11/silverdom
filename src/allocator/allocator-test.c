#include "allocator.c"
#include <stdio.h>
#include <memory.h>

// Whitebox test

#define _STR(s) #s
#define STR(s) _STR(s)
#define ASSERT(cond) { if (!(cond)) { puts("Assertion fails at " __FILE__ ":" STR(__LINE__) "\n"); panic(); } }

void panic() {
    exit(-1);
}

size_t size_ladder[] = {
   32768, 21840, 16384, 13104, 10920, 9360, 8192, 7280, 6552, 5952, 5456, 5040, 4680, 4368,
   4096, 3848, 3640, 3448, 3272, 3120, 2976, 2848, 2728, 2616, 2520, 2424, 2336, 2256, 2184, 2112,
   2048, 1984, 1920, 1856, 1792, 1728, 1664, 1600, 1536, 1472, 1408, 1344, 1280, 1216, 1152, 1088,
   1024, 960, 896, 832, 768, 704, 640, 576, 512, 448, 384, 320, 256, 192, 128, 96, 64, 32
};

void test_sizes() {
    size_t* lim_ptr = size_ladder + sizeof(size_ladder) / sizeof(size_t);
    size_t lim = 0;
    int p = -1;
    for (size_t v = 1; v < 32768; v++) {
        if (v > lim) {
            if (lim_ptr == size_ladder)
                break;
            lim = *--lim_ptr;
            p++;
            size_t page_lim = page_to_bytes(p);
            ASSERT(page_lim == lim);
        }
        size_t page = bytes_to_page(v);
        ASSERT(p == page);
    }
    size_t b = 0;
    for (size_t v = 65536; v < (2llu << 48); v <<= 1, b++) {
        ASSERT(bytes_to_buddy(v) == b);
        ASSERT(bytes_to_buddy(v - 1) == b);
        ASSERT(bytes_to_buddy(v + 1) == b + 1);
        ASSERT(buddy_to_bytes(b) == v);
    }
}

static char heap[8 * 1024 * 1024];
static AgAllocator* allocator;

void check_heap() {
    for (uint64_t i = 0; i < AG_AL_BUDDY_COUNT; i++) {
        AgAlListItem* root = &allocator->b_free[i];
        for (AgAlListItem* prev = root, *b = root->next; b != root; prev = b, b = b->next) {
            size_t ofs = ((char*)b - heap) - sizeof(AgAllocator) - 8;
            ASSERT(ofs % buddy_to_bytes(i) == 0);
            ASSERT((char*)b > heap && (char*)b < heap + sizeof(heap));
            ASSERT(b->prev == prev);
            ASSERT(((uint64_t*)b)[-1] == AG_AL_F_BUDDY | i << AG_AL_F_OFFSET);
        }
    }
    for (uint64_t i = 0; i < AG_AL_PAGE_COUNT; i++) {
        AgAlListItem* root = &allocator->p_free[i];
        for (AgAlListItem* prev = root, *b = root->next; b != root; prev = b, b = b->next) {
            size_t ofs = (((char*)b - heap) - sizeof(AgAllocator)) % 65536 - 8;
            ASSERT(ofs % page_to_bytes(i) == 0);
            ASSERT((char*)b > heap && (char*)b < heap + sizeof(heap));
            ASSERT(b->prev == prev);
            ASSERT(((uint64_t*)b)[-1] == i << AG_AL_F_OFFSET);
        }
    }
}

void* checked_alloc(size_t size) {
    void* r = ag_al_alloc(allocator, size);
    check_heap();
    return r;
}

void checked_free(void* ptr) {
    ag_al_free(allocator, ptr);
    check_heap();
}

void test() {
    allocator = ag_al_init(heap, sizeof(heap));
    char* m1 = (char*)checked_alloc(32769);
    memset(m1, 0, 32769);
    checked_free(m1);
    char* m2 = (char*)checked_alloc(32790);

    int count = 100;
    char* data[100];
    for (int i = 1; i < count; i++) {
        data[i] = (char*)checked_alloc(i * 100);
        memset(data[i], i, i * 100);
    }
    for (int i = 1; i < count; i++) {
        ASSERT(data[i][0] == i);
        ASSERT(data[i][i * 100 - 1] == i);
        checked_free(data[i]);
    }
}

int main() {
    test_sizes();
    test();
}
