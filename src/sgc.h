#ifndef SGC_H
#define SGC_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define SGC_DEBUG

typedef enum Flags {
    SLOT_UNUSED     = 0,
    SLOT_IN_USE     = 1,
    SLOT_MARKED     = 2,
    SLOT_TOMBSTONE  = 4
} Flags;

struct SGC_Slot_ {
    size_t size;
    Flags flags;
#ifdef SGC_DEBUG
    int id;
#endif
    uintptr_t address;
};

typedef struct SGC_Slot_ SGC_Slot;

#define SLOTS_MAX_LOAD 0.75
#define SLOTS_INITIAL_CAPACITY 2
#define SLOTS_GROW_FACTOR 2

typedef struct {
    void *stackBottom;
    uintptr_t minAddress;
    uintptr_t maxAddress;
    size_t bytesAllocated;
    
    int slotsCount;
    int slotsCapacity;
    SGC_Slot *slots;
#ifdef SGC_DEBUG
    int lastId;
#endif
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
