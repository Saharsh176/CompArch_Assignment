.text
.globl main
main:
    la t0, data
    flw f0, 0(t0)
    addi t1, zero, 5
    fsrm zero, t1
    fadd.s f1, f0, f0       # dynamic rm=5 is illegal
    addi a7, zero, 10
    ecall
.data
data:
    .float 1.0
