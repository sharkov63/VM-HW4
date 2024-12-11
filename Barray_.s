# cdecl calling convention
# extern void *Barray_(void *stack_top, int n);
    .global Barray
    .global Barray_
    .type Barray_, STT_FUNC
Barray_:
    pushl %ebp
    movl %esp, %ebp

    # make %esp point to virtual stack
    movl 8(%ebp), %esp
   
    # push BOXED(n) argument to virtual stack
    movl 12(%ebp), %eax
    sall $1, %eax
    addl $1, %eax
    pushl %eax

    call Barray

    movl %ebp, %esp # restore %esp
    popl %ebp
    ret
