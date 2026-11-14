#include "hazard.h"

int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs) {
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c) {
    HazardVerdict v = {0, 0};
    if (!p->forwarding) {
        uint32_t s1 = c->uses_rs1 ? c->src1 : 0;
        uint32_t s2 = c->uses_rs2 ? c->src2 : 0;
        uint32_t s3 = c->uses_rs3 ? c->src3 : 0;
        if ((p->lab4 || p->sample_mode) && c->is_system) { s1 = 10u; s2 = 17u; }
        if (produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.ctrl.dst, s1) ||
            produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.ctrl.dst, s2) ||
            produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.ctrl.dst, s3) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, s1) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, s2) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, s3) ||
            (!(p->lab4 || p->sample_mode) && (produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, s1) ||
                          produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, s2) ||
                          produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, s3)))) {
            v.stall = 1;
        }
        (void)d;
        return v;
    }
    if (p->id_ex.valid && p->id_ex.fault == PF_NONE && p->id_ex.ctrl.mem_read &&
        p->id_ex.ctrl.dst != 0 &&
        ((c->uses_rs1 && c->src1 == p->id_ex.ctrl.dst) ||
         (c->uses_rs2 && c->src2 == p->id_ex.ctrl.dst) ||
         (c->uses_rs3 && c->src3 == p->id_ex.ctrl.dst))) {
        v.stall = 1;
        v.load_use = 1;
    }
    (void)d;
    return v;
}

Operands forward_operands(const Pipeline *p, const IDEX *d) {
    Operands o = { d->rs1_val, d->rs2_val, d->rs3_val, 0, 0 };
    if (!p->forwarding || d->fault != PF_NONE) return o;
    const uint32_t src[3] = { d->ctrl.src1, d->ctrl.src2, d->ctrl.src3 };
    const int used[3] = { d->ctrl.uses_rs1, d->ctrl.uses_rs2, d->ctrl.uses_rs3 };
    uint32_t *dst[3] = { &o.a, &o.b, &o.c };
    for (int k = 0; k < 3; k++) {
        if (!used[k] || src[k] == 0) continue;
        if (produces(p->ex_mem.valid && p->ex_mem.fault == PF_NONE,
                     &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, src[k])) {
            *dst[k] = p->ex_mem.alu_result;
            o.fwd_ex++;
        } else if (produces(p->mem_wb.valid && p->mem_wb.fault == PF_NONE,
                            &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, src[k])) {
            *dst[k] = p->mem_wb.wb_val;
            o.fwd_mem++;
        }
    }
    /* ecall consumes a0 (print value) and a7 (service number) at WB.
       Count these real architectural reads for Lab 5 instrumentation, but
       do not feed them through EX or turn them into interlocks. */
    if (d->ctrl.is_system && (p->lab4 || p->sample_mode)) {
        const uint32_t sys_src[2] = {10u, 17u};
        for (int k = 0; k < 2; k++) {
            if (produces(p->ex_mem.valid && p->ex_mem.fault == PF_NONE,
                         &p->ex_mem.ctrl, p->ex_mem.ctrl.dst, sys_src[k])) o.fwd_ex++;
            else if (produces(p->mem_wb.valid && p->mem_wb.fault == PF_NONE,
                              &p->mem_wb.ctrl, p->mem_wb.ctrl.dst, sys_src[k])) o.fwd_mem++;
        }
    }
    return o;
}
