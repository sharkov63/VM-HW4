# cdecl calling convention
# extern void *Bclosure_(void *stack_top, int n, void *entry);
    .global Bclosure
    .global Bclosure_
    .type Bclosure_, STT_FUNC
Bclosure_:
    pushl %ebp
    movl %esp, %ebp

    # make %esp point to virtual stack
    movl 8(%ebp), %esp
   
    # push entry to virtual stack
    pushl 16(%ebp)
    # push BOXED(n) argument to virtual stack
    movl 12(%ebp), %eax
    sall $1, %eax
    addl $1, %eax
    pushl %eax

    call Bclosure

    movl %ebp, %esp # restore %esp
    popl %ebp
    ret

