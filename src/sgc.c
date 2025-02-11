#include "sgc.h"
#include <stdint.h>
#ifdef SGC_DEBUG
#include <stdio.h>
#endif

SGC *sgc;

/**
 * Compute a 32bit hash value of address.
 *
 * It's the FNV-1a hash function. Taken from
 * https://craftinginterpreters.com/hash-tables.html#hash-functions
 *
 * I tried different (simple ones) and this one produces the
 * least amount of collisions.
 *
 * To inform about collisions set SGC_DEBUG_HASHTABLE.
 *
 * @param address memory address to hash
 */
static uint32_t hashAddress(uintptr_t address) {
  uint32_t hash = 2166136261u;
  uint8_t *a = (uint8_t *)&address;
  hash ^= a[0];
  hash *= 16777619;
  hash ^= a[1];
  hash *= 16777619;
  hash ^= a[2];
  hash *= 16777619;
  hash ^= a[3];
  hash *= 16777619;
  return hash;
  return (13 * address) ^ (address >> 15);
}

/**
 * Find slot for address (hash table style).
 *
 * Calculate the index of the address to look up. If the slot at this index
 * has the right address, that's what we were looking for, if it's empty
 * there is no slot for this address (yet). If the slot is used for another
 * address look at the next slot, until you find the address or an empty slot.
 *
 * @param   address memory address
 * @return  slot according to address or empty slot. NULL if capacity is 0
 */
static SGC_Slot *findSlot(uintptr_t address) {
  if (sgc->slotsCapacity == 0)
    return NULL;
  uint32_t hash = hashAddress(address);
  uint32_t idx = hash % sgc->slotsCapacity;
  SGC_Slot *tombstone = NULL;
#ifdef SGC_DEBUG_HASHTABLE
  int collisiontCount = 0;
  printf(" * find slot for %p (capacity: %d, count: %d)\n", (void *)address,
         sgc->slotsCapacity, sgc->slotsCount);
#endif
  /* since load of the hash table is never above 0.75% it's garanteed we hit
   * an empty slot at some point.
   */
  while (1) {
    SGC_Slot *slot = &sgc->slots[idx];
    if (slot->address == 0) {           /* found empty slot or tombstone */
      if (slot->flags == SLOT_UNUSED) { /* found empty slot */
        /* Since the slot is unused, it's ensured that there is
         * no slot for the address we are looking for in use.
         * If we came across a tombstone return it, for reuse */
#ifdef SGC_DEBUG_HASHTABLE
        if (tombstone != NULL) {
          printf(" * reusing tombstone\n");
        }
#endif
        return tombstone != NULL ? tombstone : slot;
      } else { /* found tombstone */
        /* remember the first tombstone we step over */
        if (tombstone == NULL)
          tombstone = slot;
      }
    } else if (slot->address == address) { /* found correct slot */
      return slot;
    }
    /* slot is used for another address, so look at the next one */
    idx = (idx + 1) % sgc->slotsCapacity;
#ifdef SGC_DEBUG_HASHTABLE
    printf(" * hash table collision %d\n", ++collisiontCount);
#endif
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

  if (capacity <= oldCapacity)
    return;

  sgc->slots = malloc(capacity * sizeof(SGC_Slot));
  if (sgc->slots == NULL)
    exit(1);
  sgc->slotsCapacity = capacity;
  sgc->slotsCount = 0;

#ifdef SGC_DEBUG
  printf("Adjust slots capacity from %d to %d\n", oldCapacity, capacity);
#endif

  /* initialize new slots */
  for (int i = 0; i < sgc->slotsCapacity; i++) {
    SGC_Slot *slot = &sgc->slots[i];
    slot->size = 0;
    slot->flags = SLOT_UNUSED;
    slot->address = 0;
#ifdef SGC_DEBUG
    slot->id = -1;
#endif
  }

  /* copy old slots to new table */
  for (int i = 0; i < oldCapacity; i++) {
    SGC_Slot *slot = &oldSlots[i];
    if (slot->flags == SLOT_UNUSED || slot->flags & SLOT_TOMBSTONE) {
      /* ignore unused slots and tombstones */
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
#ifdef SGC_DEBUG_HASHTABLE
  printf(" * grow slots capacity to %d\n", newCapacity);
#endif
  adjustSlotsCapacity(newCapacity);
}

/**
 * Find slot for address. Grow table capacity if necessary.
 * @param   address
 * @return  pointer to the slot for address, initialized and ready to use.
 */
static SGC_Slot *getSlot(uintptr_t address) {
  SGC_Slot *slot = findSlot(address);
  /* if slot capacity is 0 or over loaded grow capacity and refind slot */
  if (slot == NULL ||
      (slot->address == 0 &&
       sgc->slotsCount + 1 > sgc->slotsCapacity * SLOTS_MAX_LOAD)) {
    growSlotsCapacity();
    slot = findSlot(address);
  }
  /* empty slot found, so initialize it */
  if (slot->address == 0) {
    if (slot->flags ==
        SLOT_UNUSED) { /* if the slot is a tombstone it was counted already */
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
  if (sgc->grayCount + 1 >= sgc->grayCapacity) {
    sgc->grayCapacity = sgc->grayCapacity == 0
                            ? SLOTS_INITIAL_CAPACITY
                            : sgc->grayCapacity * SLOTS_GROW_FACTOR;
    sgc->grayList =
        realloc(sgc->grayList, sgc->grayCapacity * sizeof(SGC_Slot *));
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

  printf("== SGC initialized\n");
#endif
}

/**
 * Delete a slot (replace it by a tombstone) and adjust the amount of
 * managed memory.
 * Free the managed memory address only if freeMemory is not 0.
 * @param   slot to delete
 * @param   freeMemory if not 0 the managed memory address is freed.
 *                     Otherwise not. It's for the case the memory was
 *                     already freed by a reallocation.
 */
static void freeSlotAndMemory(SGC_Slot *slot, int freeMemory) {
  sgc->bytesAllocated -= slot->size;

  if (freeMemory) {
#ifdef SGC_DEBUG
    printf("   - free #%d\n", slot->id);
#endif
    free((void *)slot->address);
  }
#ifdef SGC_DEBUG
  else {
    printf("   - remove #%d (it was freed by a reallocation)\n", slot->id);
  }
#endif

  slot->address = 0;
  slot->flags = SLOT_TOMBSTONE;
  slot->size = 0;
}

/**
 * Free memory managed by slot, and replace it by a tombstone in the
 * slot table
 * @param   slot to free
 */
static void freeSlot(SGC_Slot *slot) { freeSlotAndMemory(slot, 1); }

/**
 * Free all slots, grayList and the main struct
 */
void sgc_exit() {
#ifdef SGC_DEBUG
  printf("-- start cleaning up\n");
#endif
  /* free all used slots */
  for (int i = 0; i < sgc->slotsCapacity; i++) {
    SGC_Slot *slot = &sgc->slots[i];
    if (slot->flags & SLOT_IN_USE)
      freeSlot(slot);
  }
  /* free slots list */
  free(sgc->slots);
  /* free gray list */
  free(sgc->grayList);
  /* free main struct */
  free(sgc);
#ifdef SGC_DEBUG
  printf("-- end cleanup up\n");
  if (sgc->bytesAllocated > 0) {
    printf(" ! not all allocated bytes got free (%lu bytes are left)\n",
           sgc->bytesAllocated);
  }
  printf("== SGC exited\n");
#endif
}

/**
 * Check if a collection should be done and run it if so.
 */
static void collectIfNecessary() {
#ifdef SGC_STRESS
  sgc_collect();
#else
  /* if enough memory was allocated in total start a collection */
  if (sgc->bytesAllocated > sgc->nextGC) {
    sgc_collect();
  }
#endif
}

/* Change minAddress and maxAddress if necessary
 * @param slot  The slot that was edited
 */
static void updateMemoryAddressRange(const SGC_Slot *slot) {
  /* update minimal and maximal memory address */
  if ((uintptr_t)slot->address < sgc->minAddress) {
    sgc->minAddress = slot->address;
#ifdef SGC_DEBUG
    printf("   update min address to %p\n", (void *)sgc->minAddress);
#endif
  }
  if (slot->address + slot->size > sgc->maxAddress) {
    sgc->maxAddress = slot->address + slot->size;
#ifdef SGC_DEBUG
    printf("   update max address to %p\n", (void *)sgc->maxAddress);
#endif
  }
}

/**
 * Allocate managed memory.
 *
 * Find a slot for storing information about the memory,
 * allocate memory at the heap and store it's adress and size.
 * Start collection if a decent amount of memory was allocated.
 */
void *sgc_malloc(size_t size) {
  /* allocate requested amount of memory */
  void *address = malloc(size);
  if (address == NULL)
    return NULL;

  /* trigger the collection */
  collectIfNecessary();

  /* store information about the memory */
  SGC_Slot *slot = getSlot((uintptr_t)address);
  slot->size = size;
  slot->address = (uintptr_t)address;
  slot->flags = SLOT_IN_USE;

#ifdef SGC_DEBUG
  printf("-- allocated %lu bytes for #%d\n", size, slot->id);
#endif
  /* add size to total amout of allocated memory, for triggering
   * the next collection */
  sgc->bytesAllocated += size;

  /* update the lower and upper memory address bounds */
  updateMemoryAddressRange(slot);

  return address;
}

/**
 * Get
 */
void *sgc_realloc(void *ptr, size_t newSize) {
  /* if ptr is NULL it's a normal allocation */
  if (ptr == NULL) {
    return sgc_malloc(newSize);
  }

  /* get the slot for the memory address */
  SGC_Slot *slot = findSlot((uintptr_t)ptr);

  /* if the slot is not in use do a normal allocation */
  if (slot == NULL || !(slot->flags & SLOT_IN_USE)) {
    return sgc_malloc(newSize);
  }

  /* if new size is less than old size do nothing */
  if (slot->size >= newSize) {
    return ptr;
  }

  /* trigger collection */
  collectIfNecessary();

  /* real reallocation */
  void *newPtr = realloc(ptr, newSize);

  /* if the pointer didn't change just adjust the slot size */
  if (newPtr == ptr) {
    sgc->bytesAllocated += newSize - slot->size;

#ifdef SGC_DEBUG
    printf("-- reallocated %lu bytes (before %lu bytes) for #%d\n", newSize,
           slot->size, slot->id);
#endif
    slot->size = newSize;

    updateMemoryAddressRange(slot);

    return ptr;
  }

  /* store information about the memory */
  SGC_Slot *newSlot = getSlot((uintptr_t)newPtr);
  newSlot->size = newSize;
  newSlot->address = (uintptr_t)newPtr;
  newSlot->flags = SLOT_IN_USE;

  /* adjust the amount of allocated memory */
  sgc->bytesAllocated += newSize;

#ifdef SGC_DEBUG
  printf("-- reallocated %lu bytes for #%d (before #%d)\n", newSize,
         newSlot->id, slot->id);
#endif

  /* update lower and upper address bounds */
  updateMemoryAddressRange(newSlot);

  /* free old slot */
  freeSlotAndMemory(slot, 0);

  return newPtr;
}

/**
 * Mark slot as reachable, and finished (black in tricolor abstraction)
 * @param   slot to mark
 */
static void markSlot(SGC_Slot *slot) {
  slot->flags |= SLOT_MARKED;
#ifdef SGC_DEBUG
  printf("   > marked #%d\n", slot->id);
#endif
}

/**
 * Check if there is a pointer at the given address, and if it is managed
 * by a SGC_Slot. If so mark slot as reachable.
 */
static void checkAddress(void **ptr) {
  if (sgc->slotsCount == 0)
    return; /* return if no memory is managed */

  /* check if the value (interpreted as a memory address) is in the range of
   * managed addresses */
  uintptr_t address = (uintptr_t)*ptr;
  if (ptr == NULL || address < sgc->minAddress || address > sgc->maxAddress)
    return;

  /* check if the address is managed */
  SGC_Slot *slot = findSlot(address);
  if (slot->flags & SLOT_IN_USE) {
    /* if address is managed put slot on gray list */
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
  asm volatile("movq %%rbp, %0;" : "=m"(top));
  return top;
}

/*
 * Go through region and scan for known pointers.
 * begin and end should be aligned to sizeof(void*).
 *
 * @param begin the first address to scan
 * @param end the address of the end of the region
 */
static void scanRegion(void *begin, void *end) {
  if (begin == end)
    return;

  void **ptr;

  if (begin < end) {
    for (ptr = begin; ptr < (void **)end; ptr++) {
      checkAddress(ptr);
    }
  }

  if (end < begin) {
    for (ptr = begin; ptr > (void **)end; ptr--) {
      checkAddress(ptr);
    }
  }
}

/**
 * Scan stack.
 */
void scanStack() { scanRegion(sgc->stackBottom, getStackTop()); }

/**
 * Scan all memory regions of managed slots.
 */
void trace() {
  while (sgc->grayCount > 0) {
    /* get last element of grayList and remove it from list */
    SGC_Slot *slot = sgc->grayList[--sgc->grayCount];
    /* continue if it's already done */
    if (slot->flags & SLOT_MARKED)
      continue;
    /* scan memory managed by slot */
    scanRegion((void *)slot->address, (void *)(slot->address + slot->size));
    /* mark slot as done (black in tricolor abstraction) */
    markSlot(slot);
  }
}

/**
 * Free all non-reachable slots and remove marking by reachable one and remove
 * marking by reachable ones
 */
void sweep() {
  for (int i = 0; i < sgc->slotsCapacity; i++) {
    SGC_Slot *slot = &sgc->slots[i];
    /* ignore unused slots */
    if (slot->flags & SLOT_IN_USE) {
      /* unmark marked slots */
      if (slot->flags & SLOT_MARKED)
        slot->flags ^= SLOT_MARKED;
      /* free unmarked slots */
      else
        freeSlot(slot);
    }
  }
}

/**
 * scan, trace and sweep garbage.
 */
void sgc_collect() {
#ifdef SGC_DEBUG
  printf("-- begin collection\n");
  size_t before = sgc->bytesAllocated;
#endif
  extern char end, etext;   /* provided by the linker */
  scanRegion(&end, &etext); /* not sure why it only works correcty if end is
                               provides as first parameter */

  scanStack();
  trace();
  sweep();

  /* update amount of memory at which the next collection should be triggered */
  sgc->nextGC = sgc->bytesAllocated * HEAP_GROW_FACTOR;

#ifdef SGC_DEBUG
  printf("-- end collection\n");
  printf("   freed %lu bytes (before %lu, now: %lu)\n",
         before - sgc->bytesAllocated, before, sgc->bytesAllocated);
  printf("   next collection at %lu\n", sgc->nextGC);
#endif
}
