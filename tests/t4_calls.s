# t4: function calls. Loops are closed by 'j' (a jal, free in IF) rather than
# a backward branch, and every function ends in a 'ret' (a jalr, never
# predicted).  Shape of the run:
#   33 jal  = 13 calls + 13 loop-back j + 5 calls + 2 other j
#   18 jalr = 18 returns
#   20 conditional branches, all forward, only 2 taken
#   15 load-use stalls (the lw/add pair in `leaf`, plus two pairs in main)
.text
.globl main
main:
    addi gp, zero, 1024        # scratch word at 0x400
    addi t0, zero, 7
    sw   t0, 0(gp)
    beq  gp, zero, bad         # branch C: forward, never taken
    lw   s2, 0(gp)             # load-use pair #1
    add  s3, s2, s2
    lw   s4, 0(gp)             # load-use pair #2
    add  s5, s4, s3
    addi s0, zero, 0           # i
    addi s1, zero, 13          # n
    addi a0, zero, 0           # accumulator
    addi a2, zero, 1
    addi a3, zero, 0
    addi t5, zero, 3
    addi t6, zero, 5
    add  s6, t5, t6
    slli s7, s6, 2
    sub  s8, s7, t5
loop:
    beq  s0, s1, done          # branch A: N x13, then T (loop exit)
    jal  ra, leaf
    addi s0, s0, 1
    j    loop                  # free: jal redirected in IF
done:
    addi a1, zero, 1
    jal  ra, leaf2
    addi a1, zero, 1
    jal  ra, leaf2
    addi a1, zero, 0           # the one call that takes branch B
    jal  ra, leaf2
    addi a1, zero, 1
    jal  ra, leaf2
    addi a1, zero, 1
    jal  ra, leaf2
    j    finish
bad:
    addi a0, zero, -1
finish:
    addi a7, zero, 1           # print a0
    ecall
    addi a7, zero, 10          # exit
    ecall

# NOTE: nothing hazardous may follow the final ecall in memory: IF keeps
# fetching past it and a load-use pair there would be counted as a stall that
# costs no cycle (cycles = retired+4+2*redirects+stalls would be off by one).
leaf2:                         # called 5 times; B is taken once (a1 == 0)
    beq  a1, zero, l2z         # branch B: N N T N N
    addi a0, a0, 1
    jalr zero, 0(ra)
l2z:
    addi a0, a0, 100
    j    l2ret
l2ret:
    jalr zero, 0(ra)

leaf:                          # called 13 times
    lw   t0, 0(gp)
    add  a0, a0, t0            # load-use: 1 stall per call
    slli t1, a0, 1
    xor  t2, t1, a0
    add  a2, a2, t2
    srli t3, a2, 3
    andi t3, t3, 255
    add  a3, a3, t3
    jalr zero, 0(ra)           # ret
