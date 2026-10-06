#ifndef HOST_HEAP_CAPS_H
#define HOST_HEAP_CAPS_H
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_free free
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 0; }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 0; }
#endif
