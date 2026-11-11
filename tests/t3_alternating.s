# t3: a forward branch whose outcome alternates T N T N T N T N, inside a
# counted loop. No predictor keyed on one branch's own history can learn it.
# The N path ends in a 'j' (jal), executed 4 times.
.text
.globl main
main:
    addi t0, zero, 1       # i = 1
    addi t3, zero, 9       # bound
    addi t1, zero, 0       # sum
loop:
    andi t2, t0, 1
    bne  t2, zero, odd     # forward: taken when i is odd -> T N T N ...
    addi t1, t1, 1         # even path
    j    next              # jal
odd:
    addi t1, t1, 2
next:
    addi t0, t0, 1
    bne  t0, t3, loop      # backward loop branch: T x7, N
    mv   a0, t1
    addi a7, zero, 10
    ecall
