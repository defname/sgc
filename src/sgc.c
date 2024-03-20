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

static void sgc_freeSlot(SGC_Slot *slot) {
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
        sgc_freeSlot(last);
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

static void sgc_mark_slot(SGC_Slot *slot) {
    slot->marked = true;
#ifdef SGC_DEBUG
    printf("marked #%d\n", slot->id);
#endif
}

static void sgc_check_address(void **ptr) {
    if (ptr == NULL || (uintptr_t)*ptr < sgc->minAddress || (uintptr_t)*ptr > sgc->maxAddress) return;
    
    // naive linear search for *ptr in slots
    SGC_Slot *slot = sgc->slots;
    while (slot != NULL) {
        if (&slot->memory == *ptr) {
            sgc_mark_slot(slot);
            return;
        }
        slot = slot->next;
    }
}

void sgc_mark_stack() {
    void *t = NULL;
    void **bottom,  **top, **ptr;
    bottom = sgc->stackBottom;
    asm volatile ("movq %%rbp, %0;" : "=m" (top));


    if (top == bottom) return;

    if (top < bottom) {
        printf("order downwards\n");
        for (ptr=top; ptr < bottom; ptr++) {
            sgc_check_address(ptr);
        }
    }

    if (bottom < top) {
        printf("order upwards\n");
        for (ptr=top; ptr > bottom; ptr--) {
            printf("%p -> %p\n", ptr, *ptr);
            sgc_check_address(ptr);
        }
    }
}

void sgc_sweep() {
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
            sgc_freeSlot(toDelete);
        }
    }
}

void sgc_run() {
    sgc_mark_stack();
    sgc_sweep();
}
