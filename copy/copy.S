	.file	"copy.c"
	.globl	copy
	.type	copy, @function
copy:
    mv            t6,a1
    mv            t5,a0
    mv            a5,a2

loop:
    vsetvli       a4,a5,e8,m8,tu,mu
    vle8.v        v0,(t5)
    sub           a5,a5,a4
    add           t5,t5,a4
    vse8.v        v0,(t6)
    add           t6,t6,a4
    bnez          a5,loop

    ret

