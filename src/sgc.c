#include "sgc.h"
#include <setjmp.h>
#include <stdint.h>
#ifdef SGC_DEBUG
#include <stdio.h>
#endif

SGC *sgc;

void sgc_init_(void *stackBottom) {
    sgc = malloc(sizeof(SGC));
    sgc->stackBottom = stackBottom;
    sgc->slots = NULL;
    sgc->minAddress = UINTPTR_MAX;
    sgc->maxAddress = 0;
}

static void freeSlot(SGC_Slot *slot) {
#ifdef SGC_DEBUG
        printf("free #%d\n", slot->id);
#endif
        free(slot);
}

void sgc_exit() {
    SGC_Slot *slot = sgc->slots;
    while (slot != NULL) {
        SGC_Slot *last = slot;
        slot = slot->next;
        freeSlot(last);
    }
    free(sgc);
}

void *sgc_malloc(size_t size) {
    SGC_Slot *slot = malloc(sizeof(SGC_Slot) + size);
    if (slot == NULL) return NULL;

    slot->size = size;
    slot->marked = false;
#ifdef SGC_DEBUG
    if (sgc->slots == NULL) slot->id = 0;
    else slot->id = sgc->slots->id+1;
    printf("Allocated %lu bytes for #%d\n", size, slot->id);
#endif
    slot->next = sgc->slots;
    sgc->slots = slot;

    if ((uintptr_t)slot->memory < sgc->minAddress) {
        sgc->minAddress = (uintptr_t)slot->memory;
#ifdef SGC_DEBUG
        printf("Update min address to %p\n", (void*)sgc->minAddress);
#endif
    }
    if ((uintptr_t)(slot->memory+size) > sgc->maxAddress) {
        sgc->maxAddress = (uintptr_t)slot->memory+size;
#ifdef SGC_DEBUG
        printf("Update max address to %p\n", (void*)sgc->maxAddress);
#endif
    }

    return (void*)&slot->memory;
}

static void mark_slot(SGC_Slot *slot) {
    slot->marked = true;
#ifdef SGC_DEBUG
    printf("marked #%d\n", slot->id);
#endif
}

static void check_address(void **ptr) {
    if (ptr == NULL || (uintptr_t)*ptr < sgc->minAddress || (uintptr_t)*ptr > sgc->maxAddress) return;
    
    // naive linear search for *ptr in slots
    SGC_Slot *slot = sgc->slots;
    while (slot != NULL) {
        if (&slot->memory == *ptr) {
            mark_slot(slot);
            return;
        }
        slot = slot->next;
    }
}

/* 
 * Return a pointer to the top of the stack (usually the lowest address).
 * It's not actual the top it's the address of the callframe of this
 * function, but it should be on top of all other variables since it's
 * the last one on the call stack.
*/
void *getStackTop() {
    void *top;
    asm volatile ("movq %%rbp, %0;" : "=m" (top));
    return top;
}

/* 
 * Go through region and scan for known pointers.
 * begin and end should be aligned to sizeof(void*)
*/
static void scanRegion(void *begin, void *end) {
 
    if (begin == end) return;

    void **ptr;

    if (begin < end) {
        printf("order downwards\n");
        for (ptr=begin; ptr < (void**)end; ptr++) {
            check_address(ptr);
        }
    }

    if (end < begin) {
        printf("order upwards\n");
        for (ptr=begin; ptr > (void**)end; ptr--) {
            check_address(ptr);
        }
    }
   
}

void scanStack() {
    scanRegion(sgc->stackBottom, getStackTop());
}

void sweep() {
    SGC_Slot *previous = NULL;
    SGC_Slot *slot = sgc->slots;
    while (slot != NULL) {
        if (slot->marked) {
            slot->marked = false;
            previous = slot;
            slot = slot->next;
        }
        else {
            SGC_Slot *toDelete = slot;
            if (previous != NULL) {
                previous->next = slot->next;
            }
            else {
                sgc->slots = slot->next;
            }
            slot = slot->next;
            freeSlot(toDelete);
        }
    }
}

void sgc_run() {
    scanStack();
    sweep();
}
