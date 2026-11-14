.text
.globl main
main:
    addi t0, zero, 5
    addi t1, zero, 0
loop:
    add  t1, t1, t0
    addi t0, t0, -1
    bne  t0, zero, loop       # backward branch: mostly taken
    beq  zero, zero, forward  # forward branch: always taken
    addi s0, zero, 99         # squashed
forward:
    jal  ra, subroutine       # direct jump is resolved in IF in Lab 4
    addi s1, zero, 99         # squashed
return_here:
    addi a0, t1, 0            # 15
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
subroutine:
    addi t2, zero, 7
    jalr zero, 0(ra)           # indirect return is not predicted
