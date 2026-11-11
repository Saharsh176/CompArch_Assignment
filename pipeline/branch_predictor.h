#ifndef BRANCH_PREDICTOR_H
#define BRANCH_PREDICTOR_H

/* Lab Sheet 4 -- direction-predictor plug-in interface.
 *
 * A predictor sees exactly two things about a conditional branch: its own
 * PC and its taken-target address.  It never sees register values or the
 * real outcome until update() is called from EX. */

#include <stdint.h>

#define BP_TABLE_BITS 6
#define BP_TABLE_SIZE (1 << BP_TABLE_BITS)   /* 64 entries */

/* The table is owned by the pipeline, so predictors hold no static state
 * and several can coexist in one binary. */
typedef struct {
    uint8_t table[BP_TABLE_SIZE];
} BPState;

/* Branch PCs are word-aligned, so the low 2 bits carry no information. */
static inline int bp_index(uint32_t pc) {
    return (int)((pc >> 2) & (BP_TABLE_SIZE - 1));
}

typedef struct {
    const char *name;
    const char *description;
    void (*reset)(BPState *s);
    /* 1 = predict taken. `target` is the branch's taken address.
     * Reads state only; must never modify it. */
    int  (*predict)(const BPState *s, uint32_t pc, uint32_t target);
    /* Called in EX once the real outcome is known. Only place state changes. */
    void (*update)(BPState *s, uint32_t pc, uint32_t target, int taken);
} BranchPredictor;

/* The four predictors (bp_static.c: first two; bp_dynamic.c: last two). */
extern const BranchPredictor bp_not_taken;
extern const BranchPredictor bp_btfn;
extern const BranchPredictor bp_1bit;
extern const BranchPredictor bp_2bit;

/* Run-time selection (branch_predictor.c). */
const BranchPredictor *bp_find(const char *name);   /* NULL if unknown */
const BranchPredictor *bp_default(void);            /* 2bit */
int                    bp_count(void);
const BranchPredictor *bp_at(int i);

#endif /* BRANCH_PREDICTOR_H */
