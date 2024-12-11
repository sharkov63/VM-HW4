# cdecl calling convention
# extern void *Bsexp_(void *stack_top, int n);
    .global Bsexp
    .global Bsexp_
    .type Bsexp_, STT_FUNC
Bsexp_:
    pushl %ebp
    movl %esp, %ebp

    # make %esp point to virtual stack
    movl 8(%ebp), %esp
   
    # push BOXED(n + 1) argument to virtual stack
    movl 12(%ebp), %eax
    sall $1, %eax
    addl $3, %eax
    pushl %eax

    call Bsexp

    movl %ebp, %esp # restore %esp
    popl %ebp
    ret

