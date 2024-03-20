#include <stdint.h>
#include <stdio.h>

/**
 * I tried different ways to find the bottom and the top of the stack.
 * It shouldn't be as hard, since variables are just thrown there one-by-one
 * I thought...
 * So an easy way would be to just grab the address of the first variable
 * inside main() and whenever I want the top of the stack initialize
 * any other variable and take it's address.
 * This worked pretty well with the clang compiler, but gcc reorderes
 * variable to optimize the stack usage and I didn't find a way to
 * find the variable which makes it first onto the stack.
 * So I found this solution here: https://maplant.com/2020-04-25-Writing-a-Simple-Garbage-Collector-in-C.html
 * Just read the proper register with assembly... 
 * rsp stores the address of the top of the stack and rbp the address of
 * the current call frame, but systems may reuse it somehow for other
 * purposes.
 * However it works with clang and with gcc, which is good enough for me.
 * The advantage of this method is that there is no need to worry about
 * alignment, since these addresses seem to be perfectly aligned.
 */

int main(int argc, char **argv) {
    uintptr_t stackBottom, stackTop;
    asm volatile ("movq %%rbp, %0;" : "=m" (stackBottom));
    asm volatile ("movq %%rsp, %0;" : "=m" (stackTop));
    int foo;
    printf("bottom: %p\n", (void*)stackBottom);
    printf("top:    %p\n", (void*)stackTop);

    printf("argc:   %p\n", &argc);
    printf("argv:   %p\n", &argv);
    printf("foo:    %p\n", &foo);


    return 0;
}
