/**
 * Simple Garbage Collector
 */
#ifndef SGC_H
#define SGC_H

#include <stdint.h>
#include <stdlib.h>

// #define SGC_DEBUG  /**< show debug messages */
// #define SGC_STRESS  /**< run collection before any allocation */
// #define SGC_DEBUG_HASHTABLE  /**< inform about collisions, growing, etc */

typedef enum Flags {
  SLOT_UNUSED = 0,
  SLOT_IN_USE = 1,
  SLOT_MARKED = 2,
  SLOT_TOMBSTONE = 4
} Flags;

/**
 * Hold information about managed allocated memory.
 */
struct SGC_Slot_ {
  size_t size; /**< size of the allocated memory */
  Flags flags; /**< flags for use in the hash table */
#ifdef SGC_DEBUG
  int id; /**< identifier useful for debugging */
#endif
  uintptr_t address; /**< address of managed memory */
};
typedef struct SGC_Slot_ SGC_Slot;

#define SLOTS_MAX_LOAD                                                         \
  0.75 /**< if the hash table holding the slot informations is fuller, it will \
          be increased */
#define SLOTS_INITIAL_CAPACITY                                                 \
  8 /**< initial capacity of hash table and lists */
#define SLOTS_GROW_FACTOR                                                      \
  2 /**< if hash tables or lists become to small, they will be increased by    \
       this factor */
#define HEAP_GROW_FACTOR                                                       \
  2 /**< how much more memory to allocate before next collection */

/**
 * Main SGC struct.
 */
typedef struct {
  /* stackBottom will usually have the highest address, since stack grows from
   * high to low addresses. */
  void *stackBottom; /**< pointer to lowest part of the stack (has to be
                        aligned) */

  uintptr_t minAddress; /**< lower bound of managed allocated memory */
  uintptr_t maxAddress; /**< upper bound of managed allocated memory */

  /* used to decide when to collect garbage */
  size_t bytesAllocated; /**< number of bytes currently managed */
  size_t
      nextGC; /**< number of allocated bytes to trigger the next collection */

  /* slots hold information about allocated memory.
   * They are stored in a hash map mapping the memory address to the slot. */
  int slotsCount;    /**< number of memory slots managed */
  int slotsCapacity; /**< total capacity of the hash table holding information
                        about slots */
  SGC_Slot *slots;   /**< slots hash table */

  /* grayList is a dynamic list build during scanRegion().
   * It's a todo list with slots which associated memory still needs to be
   * scanned. */
  int grayCount;       /**< number of elements in grayList */
  int grayCapacity;    /**< capacity of grayList */
  SGC_Slot **grayList; /**< gray list (tricolor abstraction) */

#ifdef SGC_DEBUG
  int lastId; /**< used to assign unque IDs to slots for debugging */
#endif
} SGC;

/**
 * Do not use this function!
 * Use the macro sgc_init() instead.
 * @param stackBottom pointer to the bottom of the stack. This pointer
 * has to be aligned!
 */
void sgc_init_(void *stackBottom);

/**
 * Initialize SGC.
 * Has to be called before any allocations are done.
 */
/* copy callframe register to stackBottom */
#define sgc_init()                                                             \
  do {                                                                         \
    uintptr_t stackBottom;                                                     \
    asm volatile("movq %%rbp, %0;" : "=m"(stackBottom));                       \
    sgc_init_((void *)stackBottom);                                            \
  } while (0)

/**
 * Clean everything up.
 * Call this at the end of your program.
 */
void sgc_exit();

/**
 * Allocate size bytes of memory.
 * You use it exactly like malloc, but you don't have to free
 * anything by yourself.
 * @param   size number of bytes to allocate
 * @return  pointer to the allocated memory
 */
void *sgc_malloc(size_t size);

/**
 * Change the size of allocated memory.
 * If newSize is less or equal than the allocated memory for ptr
 * the function will do nothing. If a real reallocation is done
 * the returned pointer will be most likey different from ptr.
 * @param   ptr the pointer to reallocate
 * @param   newSize number of bytes to allocate
 * @return  pointer to the allocated memory
 */
void *sgc_realloc(void *ptr, size_t newSize);

/**
 * Run the garbage collector.
 * There is no need to call this function manually, but you
 * can do if you want.
 */
void sgc_collect();

#endif
