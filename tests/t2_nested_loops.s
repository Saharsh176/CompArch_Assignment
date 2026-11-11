# t2: inner loop of 3 iterations re-entered 4 times (loop re-entry is where
# 2-bit hysteresis beats a 1-bit predictor). Result: t1 = 4 * (3+2+1) = 24.
.text
.globl main
main:
    addi t0, zero, 4       # outer counter
    addi t1, zero, 0       # sum
outer:
    addi t2, zero, 3       # inner counter (re-armed every pass)
inner:
    add  t1, t1, t2
    addi t2, t2, -1
    bne  t2, zero, inner   # inner: T T N, four times
    addi t0, t0, -1
    bne  t0, zero, outer   # outer: T T T N
    addi a7, zero, 10
    ecall
