# SGC

Experiments for implementing a garbage collector (for C in this case)
to learn how one could work and which problems are encountered along
the path.

So far it's a simple conservative stop-the-world mark-and-sweep garbage collector which
operates on the stack (local variables), data segment and BSS (global variables)
and the heap (allocated memory) if
it's allocated by ``sgc_malloc()``.

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
or
```C
void *sgc_realloc(void *ptr, size_t size)
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

## Restrictions
- Allocated memory will be freed during a collection if its address is not found in memory anymore. So if you do some pointer arithmetic and discard the original
pointer to the memory it might get lost.
- No cpu registeres are checked for pointer addresses. I don't know how to easily access the registers I don't want to mess around with assembly too much.
A solution could be to flush the registers onto the stack but this isn't implemented so far. ([here is a solution](https://github.com/mkirchner/gc?tab=readme-ov-file#dumping-registers-on-the-stack))
- It only works with x64 architectures since the registers for the current call frame are read with assembly.

## How it works
In general it's pretty simple. If memory is allocated by ``sgc_malloc()`` the garbage collector remembers the address of this memory. When a collection is performed
the collector checks if there are any pointers to that address currently in use and frees it if not.

In detail it's a little bit more complex and I encountered different problems.

### Can it be freed?
I came accross the idea of learning more about garbage collectors when reading the chapter about it in the amazing book [Crafting Interpreters](https://craftinginterpreters.com/).
There everything is managed with objects that points to each other and there is a list of all objects and also a list of root objects that are in use.
So you could trace the references in the root objects to find all live objects and simply delete all the others.

In plain C it's different.
There are pointers that points to some address somewhere in the memory but there is no list of pointers you can access. BUT C let you dig through memory almost as you wish and pointers
are variables that hold a memory address which is a 32bit unsigned integer (at least in my case, this might differ between architectures I guess) and they live in the memory itself.
So one could look through memory looking for numbers that could possibly be memory addresseses and check if they are managed by the garbage collector.
```
void scanRegion(void *begin, void *end) {
  void **ptr = begin;
  for (; ptr < end; ptr++) {
    /* check if *ptr is a memory address managed by the garbage collector */
  }
}
```

### Where to look for pointers?
But where in the memory should we look for pointers? Local variables live on the stack. New variables are thrown there and if they go out of scope they are popped of...
So if there are local pointers the address they are pointing to can be found there.

A little problem is to find the bounding memory addresses of the stack. In a perfect world one could just take the address of the first local variable and the address of the last local
variable to get those addresses.
```C
int main(int argc, char **argv) {    /*  Stack: 0x00 argc           1    */
  void *stackBottom = &argc;         /*         0x04 argv           0xXX */
  /* do some stuff */                /*         0x0B stackBottom    0x00 */
  int dummy = 0;                     /*         ...                      */
  void *stackTop = &dummy;           /*         0xF0 dummy          0    */
}                                    /*         0xF4 stackTop       0xF0 */
```
Sadly this does not work.
One reason is [memory alignment](https://en.wikipedia.org/wiki/Data_structure_alignment).
The memory would look more like this.
```
 0x00          0x08          0x10          0x18    ...   0xF0          0xF8
  +-------------+-------------+-------------+-------------+-------------+-------------+
  |      | argc | argv        | stackBottom |      ...    |      |dummy | stackTop    |
  +-------------+-------------+-------------+-------------+-------------+-------------+
```
The loop need to start at the address of a pointer to iterate over possible pointers.

This approach worked with the clang compiler, but then I recognized that gcc reorders the
variables on the stack in a not predictable way. So another idea was needed.

Luckily the current address of the top of the stack is stored in a register that
can be read with a little bit of inline assembly. Also the address of the begin of the current
call frame (that's the position of the stack where a new function call starts and the local
variables of that function follow). That's pretty perfect, cause the address of the current call
frame is perfectly aligned, so the iteration can start there.
To find the top of the stack I just implemented a tiny function that gets a new call frame
which address is a upper bound (lower bound in the real world, because the stack grows from 
high to low addresses usually) for the local variables of the calling function.

### Checking if a address is managed
If the lower and upper bound of the stack is found and iterating over it works, we still need
to check if what we find there is a valid memory address to a memory region that is managed by
the garbage collector.

To do so, we store every address of allocated memory together with it's size and also the
lowest and highest address.
Then we just check if the number we are looking at is in bound of the addresses we manage and if
so if it's actually in the list of managed addresses.

For a efficient lookup the addresses are stored in a hash table together with the size of the
allocation.

## Ressources

- [Crafting Interpreters - Chapter 26: Garbage Collection](https://craftinginterpreters.com/garbage-collection.html)
- [Tiny Garbage Collector](https://github.com/orangeduck/tgc)
- [gc](https://github.com/mkirchner/gc)
- [Writing a Simple Garbage Collector in C](https://maplant.com/2020-04-25-Writing-a-Simple-Garbage-Collector-in-C.html)
