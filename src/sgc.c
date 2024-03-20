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

/**
 * Adjust capacity of slots hash table.
 * @param   capacity the new capacity of the hash table.
 *                   has to be larger than the old capacity.
 */
static void adjustSlotsCapacity(int capacity) {
    SGC_Slot *oldSlots = sgc->slots;
    int oldCapacity = sgc->slotsCapacity;
    int oldCount = sgc->slotsCount;

    if (capacity <= oldCapacity) return;

    sgc->slots = malloc(capacity * sizeof(SGC_Slot));
    if (sgc->slots == NULL) exit(1);
    sgc->slotsCapacity = capacity;
    sgc->slotsCount = 0;

#ifdef SGC_DEBUG
    printf("Adjust slots capacity from %d to %d\n", oldCapacity, capacity);
#endif

    /* initialize new slots */
    for (int i=0; i<sgc->slotsCapacity; i++) {
        SGC_Slot *slot = &sgc->slots[i];
        slot->size = 0;
        slot->flags = SLOT_UNUSED;
        slot->address = 0;
#ifdef SGC_DEBUG
        slot->id = -1;
#endif
    }

    /* copy old slots to new table */
    for (int i=0; i<oldCapacity; i++) {
        SGC_Slot *slot = &oldSlots[i];
        if (slot->flags == SLOT_UNUSED  || slot->flags & SLOT_TOMBSTONE) {
            continue;
        }
        SGC_Slot *newSlot = findSlot(slot->address);
        newSlot->address = slot->address;
#ifdef SGC_DEBUG
        newSlot->id = slot->id;
#endif
        newSlot->size = slot->size;
        newSlot->flags = slot->flags;
        sgc->slotsCount++;
    }

    /* free old table */
    free(oldSlots);
}

/**
 * Grow capacity of slots table.
 * capacity will be initialized to SLOTS_INITIAL_CAPACITY
 * or multiplied with SLOTS_GROW_FACTOR
 */
static void growSlotsCapacity() {
    int newCapacity = sgc->slotsCapacity == 0
        ? SLOTS_INITIAL_CAPACITY
        : sgc->slotsCapacity * SLOTS_GROW_FACTOR;
    adjustSlotsCapacity(newCapacity);
}

/**
 * Find slot for address. Grow table capacity if necessary.
 * @param   address
 * @return  pointer to the slot for address, initialized and ready to use.
 */
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

/**
 * Add slot to gray list (tricolor abstraction) 
 * @param   slot to add to gray list
 */
void markGray(SGC_Slot *slot) {
    /* grow gray list if necessary */
    if (sgc->grayCount+1 >= sgc->grayCapacity) {
        sgc->grayCapacity = sgc->grayCapacity == 0 ? SLOTS_INITIAL_CAPACITY : sgc->grayCapacity * SLOTS_GROW_FACTOR;
        sgc->grayList = realloc(sgc->grayList, sgc->grayCapacity * sizeof(SGC_Slot*));
    }
    /* add slot to list */
    sgc->grayList[sgc->grayCount++] = slot;
}

void sgc_init_(void *stackBottom) {
    sgc = malloc(sizeof(SGC));
    sgc->stackBottom = stackBottom;
    sgc->minAddress = UINTPTR_MAX;
    sgc->maxAddress = 0;
    sgc->bytesAllocated = 0;
    sgc->nextGC = 1024;
    
    sgc->slots = NULL;
    sgc->slotsCount = 0;
    sgc->slotsCapacity = 0;

    sgc->grayCount = 0;
    sgc->grayCapacity = 0;
    sgc->grayList = NULL;

#ifdef SGC_DEBUG
    sgc->lastId = 0;
#endif
}

/**
 * free memory managed by slot, and replace it by a tombstone in the
 * slot table
 * @param   slot to free
 */
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

#ifdef SGC_STRESS
    sgc_collect();
#else
    if (sgc->nextGC > sgc->bytesAllocated) {
        sgc_collect();
    }
#endif

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

/**
 * Mark slot as reachable, and finished (black in tricolor abstraction)
 * @param   slot to mark
 */
static void markSlot(SGC_Slot *slot) {
    slot->flags |= SLOT_MARKED;
#ifdef SGC_DEBUG
    printf("marked #%d\n", slot->id);
#endif
}

/**
 * Check if there is a pointer at the given address, and if it is managed
 * by a SGC_Slot. If so mark slot as reachable.
 */
static void checkAddress(void **ptr) {
    if (ptr == NULL || (uintptr_t)*ptr < sgc->minAddress || (uintptr_t)*ptr > sgc->maxAddress) return;
    if (sgc->slotsCount == 0) return;

    SGC_Slot *slot = findSlot((uintptr_t)*ptr);
    if (slot->flags & SLOT_IN_USE) {
        markGray(slot);
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
        for (ptr=begin; ptr < (void**)end; ptr++) {
            checkAddress(ptr);
        }
    }

    if (end < begin) {
        for (ptr=begin; ptr > (void**)end; ptr--) {
            checkAddress(ptr);
        }
    }
   
}

/**
 * Scan stack.
 */
void scanStack() {
    scanRegion(sgc->stackBottom, getStackTop());
}

/**
 * Scan all memory regions of managed slots.
 */
void trace() {
    while (sgc->grayCount > 0) {
        SGC_Slot *slot = sgc->grayList[--sgc->grayCount];
        if (slot->flags & SLOT_MARKED) continue;
        scanRegion((void*)slot->address, (void*)(slot->address+slot->size));
        markSlot(slot);
    }
}

/**
 * Free all non-reachable slots and remove marking by reachable one and remove marking by reachable ones
 */
void sweep() {
    for (int i=0; i<sgc->slotsCapacity; i++) {
        SGC_Slot *slot = &sgc->slots[i];
        if (slot->flags & SLOT_IN_USE) {
            if (slot->flags & SLOT_MARKED) slot->flags ^= SLOT_MARKED;
            else freeSlot(slot);
        }
    }
}

void sgc_collect() {
#ifdef SGC_DEBUG
    printf("-- begin collection --\n");
    size_t before = sgc->bytesAllocated;
#endif
    scanStack();
    trace();
    sweep();

    sgc->nextGC = sgc->bytesAllocated * HEAP_GROW_FACTOR;

#ifdef SGC_DEBUG
    printf("-- end collection --\n");
    printf("   freed %lu bytes (before %lu)\n", sgc->bytesAllocated-before, before);
#endif
}
