#include "sgc.h"
#include <stdint.h>
#ifdef SGC_DEBUG
#include <stdio.h>
#endif

SGC *sgc;

static uint32_t hashAddress(uintptr_t address) {
    return (uint32_t)address;
}

/**
 * Find slot for address (hash table style).
 * @param   address memory address
 * @return  slot according to address or empty slot. NULL if capacity is 0
 */
static SGC_Slot* findSlot(uintptr_t address) {
    if (sgc->slotsCapacity == 0) return NULL;
    uint32_t hash = hashAddress(address);
    uint32_t idx = hash % sgc->slotsCapacity;
    SGC_Slot *tombstone = NULL;
    /* since load of the hash table is never above 0.75% it's garanteed we hit
     * an empty slot at some point.
     */
    while (1) {
        SGC_Slot *slot = &sgc->slots[idx];
        if (slot->address == 0) {
            if (slot->flags == SLOT_UNUSED) {
                return tombstone != NULL ? tombstone : slot;
            }
            else {
                if (tombstone == NULL) tombstone = slot;
            }
        }
        else if (slot->address == address) {
            return slot;
        }
        idx = (idx + 1) % sgc->slotsCapacity;
    }
}

static void adjustSlotsCapacity(int capacity) {
    SGC_Slot *oldSlots = sgc->slots;
    int oldCapacity = sgc->slotsCapacity;
    int oldCount = sgc->slotsCount;

    sgc->slots = malloc(capacity * sizeof(SGC_Slot));
    if (sgc->slots == NULL) exit(1);
    sgc->slotsCapacity = capacity;
    sgc->slotsCount = 0;
    
    /* initialize new slots */
    for (int i=0; i<sgc->slotsCapacity; i++) {
        SGC_Slot *slot = &sgc->slots[i];
        slot->size = 0;
        slot->flags = SLOT_UNUSED;
        slot->address = 0;
        slot->id = -1;
    }

    /* copy old slots to new table */
    for (int i=0; i<oldCapacity; i++) {
        SGC_Slot *slot = &oldSlots[i];
        if (slot->flags == SLOT_UNUSED  || slot->flags | SLOT_TOMBSTONE) {
            continue;
        }
        SGC_Slot *newSlot = findSlot(slot->address);
        newSlot->address = slot->address;
        newSlot->id = slot->id;
        newSlot->size = slot->size;
        newSlot->flags = slot->flags;
        sgc->slotsCount++;
    }

    /* free old table */
    free(oldSlots);
}

static void growSlotsCapacity() {
    int newCapacity = sgc->slotsCapacity == 0
        ? SLOTS_INITIAL_CAPACITY
        : sgc->slotsCapacity * SLOTS_GROW_FACTOR;
    adjustSlotsCapacity(newCapacity);
}

static SGC_Slot* getSlot(uintptr_t address) {
    SGC_Slot *slot = findSlot(address);
    /* if slot capacity is 0 or over loaded grow capacity and refind slot */
    if (slot == NULL ||
        (slot->address == 0 && sgc->slotsCount+1 > sgc->slotsCapacity * SLOTS_MAX_LOAD)
    ) {
        growSlotsCapacity();
        slot = findSlot(address);
    }
    /* empty slot found, so initialize it */
    if (slot->address == 0) {
        if (slot->flags == SLOT_UNUSED) { /* if the slot is a tombstone it's counted already */
            sgc->slotsCount++; 
        }
        slot->address = address;
        slot->flags = SLOT_IN_USE;
#ifdef SGC_DEBUG
        slot->id = sgc->lastId++;
#endif
    }
    return slot;
}

void sgc_init_(void *stackBottom) {
    sgc = malloc(sizeof(SGC));
    sgc->stackBottom = stackBottom;
    sgc->minAddress = UINTPTR_MAX;
    sgc->maxAddress = 0;
    sgc->bytesAllocated = 0;
    
    sgc->slots = NULL;
    sgc->slotsCount = 0;
    sgc->slotsCapacity = 0;

#ifdef SGC_DEBUG
    sgc->lastId = 0;
#endif
}

static void freeSlot(SGC_Slot *slot) {
#ifdef SGC_DEBUG
    printf("free #%d\n", slot->id);
#endif
    sgc->bytesAllocated -= slot->size;    
    free((void*)slot->address);
    slot->address = 0;
    slot->flags = SLOT_TOMBSTONE;
    slot->size = 0;
}

void sgc_exit() {
    for (int i=0; i<sgc->slotsCapacity; i++) {
        SGC_Slot *slot = &sgc->slots[i];
        if (slot->flags & SLOT_IN_USE)
            freeSlot(slot);
    }
    free(sgc->slots);
    free(sgc);
}

void *sgc_malloc(size_t size) {
    void *address = malloc(size);
    if (address == NULL) return NULL;

    SGC_Slot *slot = getSlot((uintptr_t)address);
    slot->size = size;
    slot->address = (uintptr_t)address;
    slot->flags = SLOT_IN_USE;

#ifdef SGC_DEBUG
    printf("Allocated %lu bytes for #%d\n", size, slot->id);
#endif
    sgc->bytesAllocated += size;

    if ((uintptr_t)slot->address < sgc->minAddress) {
        sgc->minAddress = slot->address;
#ifdef SGC_DEBUG
        printf("Update min address to %p\n", (void*)sgc->minAddress);
#endif
    }
    if (slot->address+size > sgc->maxAddress) {
        sgc->maxAddress = slot->address+size;
#ifdef SGC_DEBUG
        printf("Update max address to %p\n", (void*)sgc->maxAddress);
#endif
    }

    return address;
}

static void mark_slot(SGC_Slot *slot) {
    slot->flags |= SLOT_MARKED;
#ifdef SGC_DEBUG
    printf("marked #%d\n", slot->id);
#endif
}

static void check_address(void **ptr) {
    if (ptr == NULL || (uintptr_t)*ptr < sgc->minAddress || (uintptr_t)*ptr > sgc->maxAddress) return;
    if (sgc->slotsCount == 0) return;

    SGC_Slot *slot = findSlot((uintptr_t)*ptr);
    if (slot->flags & SLOT_IN_USE) {
        mark_slot(slot);
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
    for (int i=0; i<sgc->slotsCapacity; i++) {
        SGC_Slot *slot = &sgc->slots[i];
        if (slot->flags & SLOT_IN_USE && !(slot->flags & SLOT_MARKED)) {
            freeSlot(slot);
        }
    }
}

void sgc_run() {
    scanStack();
    sweep();
}