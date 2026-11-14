.text
.globl main
main:
    addi t0, zero, 3
    addi t0, t0, 4
    addi t0, t0, 5
    addi t1, t0, 1
    addi t1, t1, 2
    la t4, data
    sw t1, 0(t4)
    lw t2, 0(t4)
    add t3, t2, t1
    addi a0, t3, 0
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
.data
data:
    .word 0
