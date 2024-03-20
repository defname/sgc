#ifndef SGC_H
#define SGC_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define SGC_DEBUG


#define sgc_init() do { \
    uintptr_t stackBottom; \
    asm volatile ("movq %%rbp, %0;" : "=m" (stackBottom)); \
    sgc_init_((void*)stackBottom); \
} while(0)


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
    SGC_Slot *slots;
} SGC;

void sgc_init_(void *stackBottom);
void sgc_exit();

void *sgc_malloc(size_t size);
void sgc_run();

#endif
