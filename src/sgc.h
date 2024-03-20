#ifndef SGC_H
#define SGC_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define SGC_DEBUG


struct SGC_Slot_ {
    size_t size;
    bool marked;
    struct SGC_Slot_ *next;
#ifdef SGC_DEBUG
    int id;
#endif
    char memory[];
};

typedef struct SGC_Slot_ SGC_Slot;

typedef struct {
    void *stackBottom;
    uintptr_t minAddress;
    uintptr_t maxAddress;
    size_t bytesAllocated;
    SGC_Slot *slots;
} SGC;

/**
 * Do not use this function!
 * Use the macro sgc_init() instead.
 */
void sgc_init_(void *stackBottom);

/**
 * Initialize SGC.
 * Has to be called before any allocations are done.
*/
#define sgc_init() do { \
    uintptr_t stackBottom; \
    asm volatile ("movq %%rbp, %0;" : "=m" (stackBottom)); \
    sgc_init_((void*)stackBottom); \
} while(0)

/**
 * Clean everything up.
 * Call this at the end of your program.
*/
void sgc_exit();

/**
 * Allocate size bytes of memory.
 * You use it exactly like malloc, but you don't have to free
 * anything by yourself.
*/
void *sgc_malloc(size_t size);

/**
 * Run the garbage collector.
 * There is no need to call this function manually, but you
 * can do if you want.
*/
void sgc_run();

#endif
