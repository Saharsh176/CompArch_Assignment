#include "control.h"
static uint32_t regno(uint32_t r, int fp) { return fp ? 32u + (r & 31u) : (r & 31u); }
ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h) {
    ControlSignals c = {0};
    uint32_t fl = h->flags;
    c.rd_fp = !!(fl & IF_RD_FP);
    c.rs2_fp = !!(fl & IF_RS2_FP);
    c.dst = regno(i->rd, c.rd_fp);
    c.src1 = regno(i->rs1, !!(fl & IF_RS1_FP));
    c.src2 = regno(i->rs2, !!(fl & IF_RS2_FP));
    c.src3 = regno(i->rs3, !!(fl & IF_RS3_FP));
    switch (h->format) {
        case FMT_R:
            c.reg_write=1; c.wb_src=WB_ALU; c.uses_rs1=1; c.uses_rs2=1; break;
        case FMT_R2:
            c.reg_write=1; c.wb_src=WB_ALU; c.uses_rs1=1; break;
        case FMT_R4:
            c.reg_write=1; c.wb_src=WB_ALU; c.uses_rs1=1; c.uses_rs2=1; c.uses_rs3=1; break;
        case FMT_I: case FMT_I_SHAMT:
            c.reg_write=1; c.wb_src=WB_ALU; c.uses_rs1=1; break;
        case FMT_U: c.reg_write=1; c.wb_src=WB_ALU; break;
        case FMT_I_LOAD:
            c.reg_write=1; c.wb_src=WB_MEM; c.mem_read=1; c.uses_rs1=1; break;
        case FMT_S:
            c.mem_write=1; c.uses_rs1=1; c.uses_rs2=1; break;
        case FMT_B:
            c.is_branch=1; c.uses_rs1=1; c.uses_rs2=1; break;
        case FMT_J:
            c.reg_write=1; c.wb_src=WB_PC4; c.is_jump=1; break;
        case FMT_I_JALR:
            c.reg_write=1; c.wb_src=WB_PC4; c.is_jump=1; c.uses_rs1=1; break;
        case FMT_I_SYS: c.is_system=1; break;
        case FMT_CSR:
            c.reg_write=1; c.wb_src=WB_ALU; c.uses_rs1=1; break;
        default: break;
    }
    /* The registry's metadata decides which operands are real. */
    if (h->format == FMT_R || h->format == FMT_R2 || h->format == FMT_R4 ||
        h->format == FMT_I_LOAD || h->format == FMT_S) {
        if (h->format == FMT_R || h->format == FMT_R2 || h->format == FMT_R4) {
            c.uses_rs1 = 1;
            c.uses_rs2 = h->format == FMT_R || h->format == FMT_R4;
            c.uses_rs3 = h->format == FMT_R4;
        }
    }
    if (h->format == FMT_CSR) c.uses_rs1 = (i->funct3 & 4u) == 0; /* register forms only */
    return c;
}
