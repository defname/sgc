/**
 * Just a wild test programm for SGC.
 *
 * Allocates memory on different ways and tries to access it
 * at some points to check if it still works.
 */
#include <stdio.h>
#include <stdlib.h>

#include "sgc.h"


/**
 * Print the address of var together with some additional information.
 * Used for debugging
 */
#define PRINT_ADDR(var) do { \
    char *ptr = (char*)&var; \
    printf("%4s %p   %2x\n", #var, ptr, (*ptr) & 0xff); \
    ptr++; \
    for (; ptr<(char*)(&var)+sizeof(var); ptr++) { \
        printf("     %p   %2x\n", ptr, *ptr & 0xff); \
    } \
    printf("-------------------\n"); \
} while(0)

/**
 * Allocate fixed amout of memory.
 */
void test() {
    void *foo = sgc_malloc(199);
}

typedef struct {
    void *foo;
    int *bar;
} HeapStuff;

/**
 * Allocate memory and return the pointer.
 */
HeapStuff *newHeapStuff() {
    HeapStuff *s = sgc_malloc(sizeof(HeapStuff));
    s->foo = sgc_malloc(1234);
    return s;
}

/**
 * Allocate memory recursively.
 */
void recursiveAllocationsFunction(int i, HeapStuff *p) {
    printf(">> recursive allocation function call %d\n", i);
    p->foo = sgc_malloc(sizeof(HeapStuff) + 100);
    p->bar = sgc_malloc(sizeof(int));

    newHeapStuff();

    if (i == 0) return;
    recursiveAllocationsFunction(i-1, (HeapStuff*)p->foo);
    *p->bar = 1;
}

/* variable in the data segment */
void *bss1 = NULL;

int main(int argc, char **argv) {
    sgc_init();
    bss1 = sgc_malloc(321);
    static void *bss2;  /* variable in the BSS */
    bss2 = sgc_malloc(123);

    int *a = (int*)0x0f0f0f0f;
    void *b = &a;

    for (int i=0; i<100; i++)
        recursiveAllocationsFunction(10, sgc_malloc(sizeof(HeapStuff)));

    char bar = 1;
    void **p = NULL;
 
    int *foo = (int*)sgc_malloc(100*sizeof(int));
    void *blub = sgc_malloc(1000);
    test();

    HeapStuff *s = newHeapStuff();
    void *blabla = sgc_malloc(23);

    *(int*)bss1 = 1;
    *(int*)bss2 = 1;
    sgc_exit();
}
