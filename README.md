# SGC

Experiments for implementing a garbage collector (for C in this case)
to learn how one could work and which problems are encountered along
the path.

So far it's a simple stop-the-world mark-and-sweep garbage collector which
operates on the stack (local variables) and the heap (allocated memory) if
it's allocated by ``sgc_malloc()``. It does not look through other parts
of the memory.

It's tested (just a little bit) with the [clang](https://clang.llvm.org/)
and the [GCC](https://gcc.gnu.org/) compiler.

## Usage
Initialize the garbage collector before any memory allocations or pointer
declarations with
```C
void sgc_init()
```
then use
```C
void *sgc_malloc(size_t size)
```
to allocate memory and call
```C
void sgc_exit()
```
to cleanup at the very end.

For compilation you can define ``SGC_DEBUG`` and ``SGC_DEBUG_HASHTABLE``
to show debug messages and
``SGC_STRESS`` to collect garbage at every allocation.

For example:
```
gcc -DSGC_DEBUG -o test src/*.c
```

## Example

```C
#include "sgc.h"

void heapStuff() {
  void *p = sgc_malloc(123);
}

int main() {
  /* initialize the garbage collector */
  sgc_init();
  /* do not worry about freeing memory */
  heapStuff();
  /* tell the garbage collector to do a final cleanup */
  sgc_exit();
}
```

## Ressources

- [Crafting Interpreters - Chapter 26: Garbage Collection](https://craftinginterpreters.com/garbage-collection.html)
- [Tiny Garbage Collector](https://github.com/orangeduck/tgc)
- [gc](https://github.com/mkirchner/gc)
- [Writing a Simple Garbage Collector in C](https://maplant.com/2020-04-25-Writing-a-Simple-Garbage-Collector-in-C.html)
