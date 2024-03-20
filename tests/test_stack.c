#include <stdint.h>
#include <stdio.h>

#define PRINT_ADDR(var) do { \
    char *ptr = (char*)&var; \
    printf("%4s %p   %2x\n", #var, ptr, (*ptr) & 0xff); \
    ptr++; \
    for (; ptr<(char*)(&var)+sizeof(var); ptr++) { \
        printf("     %p   %2x\n", ptr, *ptr & 0xff); \
    } \
    printf("-------------------\n"); \
} while(0)

int main(int argc, char **argv) {
    void *a = NULL;
    char b = 1;
    uint32_t c = 0x11223344;
    void *d = (void*)0x1020304050607080;

    PRINT_ADDR(d);
    PRINT_ADDR(c);
    PRINT_ADDR(b);
    PRINT_ADDR(a);
    PRINT_ADDR(argv);
    PRINT_ADDR(argc);
}
