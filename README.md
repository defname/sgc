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
```
void sgc_init()
```
then use
```
void *sgc_malloc(size_t size)
```
to allocate memory and call
```
void sgc_exit()
```
to cleanup at the very end.

For compilation you can define ``SGC_DEBUG`` to show debug messages and
``SGC_STRESS`` to collect garbage at every allocation.

For example:
```
gcc -DSGC_DEBUG -o test src/*.c
```
