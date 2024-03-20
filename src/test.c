#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


/**
 * github.com/orangeduck/tgc
 */
static void *stackTop;

void *stackBottom() {
    int *x;
    return &x;
}

void *getStackBottom() {
    jmp_buf env;
    setjmp(env);
    void *(*volatile f)() = stackBottom;
    return f();
}
/**********************/

#define PRINTADDR(var, format) do { \
    printf("%p ->  "#var" "#format" (size: %lu)\n", &var, var, sizeof(var));  \
} while(0)

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


int main(int argc, char **argv) {
    sgc_init();

    stackTop = &argc;
    int *a = (int*)0x0f0f0f0f;
    void *b = &a;

    char bar = 1;
    void **p = NULL;
    void **top = stackTop;
    void **bottom = getStackBottom();
 
    //for (p = bottom; p <= top; p++) {
    //}

    int *foo = (int*)sgc_malloc(100*sizeof(int));
    void *blub = sgc_malloc(1000);
    test();

    sgc_run();
    printf("...\n");
    sgc_exit();
}
