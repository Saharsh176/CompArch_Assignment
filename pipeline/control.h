#ifndef CONTROL_H
#define CONTROL_H
#include "decoder.h"
#include "isa_registry.h"
typedef enum { WB_NONE=0, WB_ALU, WB_MEM, WB_PC4 } WbSrc;
typedef struct {
    int reg_write;
    WbSrc wb_src;
    int mem_read, mem_write, is_branch, is_jump, is_system;
    int uses_rs1, uses_rs2, uses_rs3;
    uint32_t src1, src2, src3, dst; /* unified: x1..x31=1..31, f0..f31=32..63, x0=0 */
    int rd_fp, rs2_fp;
} ControlSignals;
ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h);
#endif
