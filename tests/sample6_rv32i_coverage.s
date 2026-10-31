# Directed RV32I pipeline coverage: ALU, shifts, memory, branches and jumps.
.text
.globl main
main:
    addi x1, x0, 12
    addi x2, x0, 3
    add  x3, x1, x2
    sub  x4, x1, x2
    and  x5, x1, x2
    or   x6, x1, x2
    xor  x7, x1, x2
    sll  x8, x2, x2
    srl  x9, x1, x2
    sra  x10, x1, x2
    slt  x11, x2, x1
    sltu x12, x2, x1
    addi x13, x0, -1
    andi x14, x13, 15
    ori  x15, x0, 16
    xori x16, x15, 3
    slti x17, x2, 4
    sltiu x18, x2, -1
    slli x19, x2, 2
    srli x20, x1, 2
    srai x21, x13, 2
    lui  x22, 1
    auipc x23, 0

    li   x24, data_area
    addi x25, x0, -10
    sb   x25, 0(x24)
    sh   x25, 2(x24)
    sw   x25, 4(x24)
    lb   x26, 0(x24)
    lh   x27, 2(x24)
    lw   x28, 4(x24)
    lbu  x29, 0(x24)
    lhu  x30, 2(x24)

    beq  x1, x1, branch_ok
    addi x31, x0, 99       # must be flushed
branch_ok:
    bne  x1, x2, bne_ok
    addi x31, x0, 98
bne_ok:
    blt  x2, x1, blt_ok
    addi x31, x0, 97
blt_ok:
    bge  x1, x2, bge_ok
    addi x31, x0, 96
bge_ok:
    bltu x2, x1, bltu_ok
    addi x31, x0, 95
bltu_ok:
    bgeu x1, x2, bgeu_ok
    addi x31, x0, 94
bgeu_ok:
    jal  x5, jal_target
    addi x31, x0, 93       # must be flushed
jal_target:
    li   x6, jalr_target
    jalr x7, 0(x6)
    addi x31, x0, 92       # must be flushed
jalr_target:
    addi a7, x0, 10
    ecall

.data
data_area:
    .word 0, 0
