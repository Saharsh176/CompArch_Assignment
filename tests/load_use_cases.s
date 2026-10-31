# Targeted tests for the four load-use forms listed in Lab Sheet 3, Section 7.
.text
.globl main
main:
    addi gp, zero, 256

    # Initialise data used by the four load-use cases.
    addi t0, zero, 7
    sw   t0, 0(gp)
    addi t0, zero, 0
    sw   t0, 4(gp)
    addi t0, zero, 256
    sw   t0, 12(gp)
    addi t0, zero, 42
    sw   t0, 256(zero)

    # 1. Load followed by an ALU consumer (rs1).
    lw   t1, 0(gp)
    addi t2, t1, 1

    # 2. Load followed by a branch consumer (rs1).
    lw   t3, 4(gp)
    beq  t3, zero, branch_ok
    addi s0, zero, 99       # must be squashed
branch_ok:

    # 3. Load followed by a store-data consumer (rs2).
    lw   t4, 0(gp)
    sw   t4, 8(gp)

    # 4. Load followed by a load-address-base consumer (rs1).
    lw   t5, 12(gp)
    lw   t6, 0(t5)

    addi a7, zero, 10
    ecall
