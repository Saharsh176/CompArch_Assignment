.text
.globl main
main:
    la t0, data
    flw f0, 0(t0)
    flw f1, 4(t0)
    fadd.s f2, f0, f1      # f2 = 3
    fmul.s f2, f2, f1      # overwrite f2 = 6
    fadd.s f3, f2, f0      # must see newest f2 = 7
    fmadd.s f4, f3, f1, f2 # 7*2+6 = 20; tests rs3 forwarding
    fsw f4, 8(t0)
    flw f5, 8(t0)
    fcvt.w.s a0, f5, rtz
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
.data
data:
    .float 1.0
    .float 2.0
    .word 0
