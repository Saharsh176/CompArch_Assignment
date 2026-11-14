.text
.globl main
main:
    la   t0, data
    flw  f0, 0(t0)          # -0
    flw  f1, 4(t0)          # +0
    flw  f2, 8(t0)          # +1
    flw  f3, 12(t0)         # -1
    flw  f4, 16(t0)         # qNaN
    flw  f5, 20(t0)         # sNaN
    flw  f6, 24(t0)         # +inf
    flw  f7, 28(t0)         # -inf
    flw  f8, 32(t0)         # 2.5
    flw  f9, 36(t0)         # -2.5

    fmin.s f10, f0, f1     # must preserve -0
    fmax.s f11, f0, f1     # must preserve +0
    fsgnj.s f12, f1, f0    # sign from -0 -> -0
    fsgnjn.s f13, f1, f0   # inverted sign -> +0
    fsgnjx.s f14, f0, f1   # XOR signs -> -0
    fclass.s t1, f0        # -zero -> bit 3
    fclass.s t2, f6        # +inf -> bit 7
    fclass.s t3, f4        # qNaN -> bit 9
    fclass.s t4, f5        # sNaN -> bit 8

    fsflags zero, zero
    fadd.s f15, f5, f2     # sNaN -> canonical NaN, NV
    frflags s1
    fsflags zero, zero
    feq.s a0, f4, f4       # quiet NaN comparison: false, no NV
    frflags s2
    fsflags zero, zero
    flt.s a1, f4, f2       # signalling comparison: false, NV
    frflags s3
    fsflags zero, zero
    fmin.s f16, f4, f2     # numeric operand wins, no NV for qNaN
    frflags s4
    fsflags zero, zero
    fmax.s f17, f5, f2     # numeric operand wins, NV for sNaN
    frflags s5

    fsflags zero, zero
    fdiv.s f18, f2, f1     # finite / zero -> DZ
    frflags s6
    fsflags zero, zero
    fsqrt.s f19, f3        # sqrt(-1) -> NV
    frflags s7

    fcvt.w.s s8, f8, rne  # 2
    fcvt.w.s s9, f9, rne  # -2
    fcvt.w.s s10, f9, rdn # -3
    fcvt.w.s s11, f9, rmm # -3
    li   t5, 16777217
    fcvt.s.w f20, t5, rne # 16777216 (ties to even)
    fcvt.s.w f21, t5, rmm # 16777218 (ties away)

    # Fused cancellation: (1+2^-23)*(1-2^-23)-1 = -2^-46 exactly.
    flw f22, 40(t0)
    flw f23, 44(t0)
    flw f24, 48(t0)
    fmadd.s f25, f22, f23, f24
    fmv.x.w t6, f25

    flw f26, 52(t0)        # +2^-24
    flw f27, 56(t0)        # -2^-24
    fadd.s f28, f2, f26, rne
    fadd.s f29, f2, f26, rmm
    fsub.s f30, f3, f26, rmm
    fsub.s f31, f3, f26, rne

    addi a7, zero, 10
    ecall

.data
data:
    .word 0x80000000
    .word 0x00000000
    .float 1.0
    .float -1.0
    .word 0x7fc00000
    .word 0x7fa00000
    .word 0x7f800000
    .word 0xff800000
    .float 2.5
    .float -2.5
    .word 0x3f800001
    .word 0x3f7ffffe
    .float -1.0
    .word 0x33800000
    .word 0xb3800000
