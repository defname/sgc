#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


#define CHECK_ADDR_EQ(var, format) \
    if (p == (void*)&var) { \
        printf("Found "#var" at %p ("#format")\n", p, *p); \
    }


#include "sgc.h"

#define PRINT_ADDR(var) do { \
    char *ptr = (char*)&var; \
    printf("%4s %p   %2x\n", #var, ptr, (*ptr) & 0xff); \
    ptr++; \
    for (; ptr<(char*)(&var)+sizeof(var); ptr++) { \
        printf("     %p   %2x\n", ptr, *ptr & 0xff); \
    } \
    printf("-------------------\n"); \
} while(0)

void test() {
    void *foo = sgc_malloc(199);
}


typedef struct {
    void *foo; 
} HeapStuff;


HeapStuff *newHeapStuff() {
    HeapStuff *s = sgc_malloc(sizeof(HeapStuff));
    s->foo = sgc_malloc(1234);
    return s;
}

void recursiveAllocationsFunction(int i, HeapStuff *p) {
    printf(">> recursive allocation function call %d\n", i);
    p->foo = sgc_malloc(sizeof(HeapStuff) + 100);

    newHeapStuff();

    if (i == 0) return;
    recursiveAllocationsFunction(i-1, (HeapStuff*)p->foo);
}

void *bss1;

int main(int argc, char **argv) {
    sgc_init();
    bss1 = sgc_malloc(321);
    static void *bss2;
    bss2 = sgc_malloc(123);
    sgc_collect();
    sgc_exit();
    return 0;

    int *a = (int*)0x0f0f0f0f;
    void *b = &a;

    for (int i=0; i<100; i++)
        recursiveAllocationsFunction(500, sgc_malloc(sizeof(HeapStuff)));

    char bar = 1;
    void **p = NULL;
 
    //for (p = bottom; p <= top; p++) {
    //}

    int *foo = (int*)sgc_malloc(100*sizeof(int));
    void *blub = sgc_malloc(1000);
    test();

    HeapStuff *s = newHeapStuff();
    void *blabla = sgc_malloc(23);
    sgc_exit();
}
