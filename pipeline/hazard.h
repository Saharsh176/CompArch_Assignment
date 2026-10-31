#ifndef HAZARD_H
#define HAZARD_H

#include "pipeline_state.h"

typedef struct {
    int stall;      /* hold ID, bubble EX, freeze IF */
    int load_use;   /* producer is a load still in EX */
} HazardVerdict;

typedef struct {
    uint32_t a, b;  /* operands EX should actually use */
    int fwd_ex, fwd_mem;
} Operands;

int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs);
HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c);
Operands forward_operands(const Pipeline *p, const IDEX *d);

#endif
