/* Task 1 -- the two static predictors.  No state, no learning. */
#include "branch_predictor.h"
#include <string.h>

/* Shared by both static predictors: nothing to remember, so the table is
 * simply zeroed (and never read). */
static void reset_none(BPState *s) {
    memset(s, 0, sizeof(*s));
}

static void update_none(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)s; (void)pc; (void)target; (void)taken;
}

/* ---- always not taken: what the pipeline did before this lab ---------- */
static int predict_not_taken(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s; (void)pc; (void)target;
    return 0;
}

const BranchPredictor bp_not_taken = {
    "not-taken",
    "static: always predict not taken",
    reset_none,
    predict_not_taken,
    update_none,
};

/* ---- backward taken, forward not taken --------------------------------
 * Loops branch backwards and are taken on every iteration but the last;
 * forward branches usually skip a rarely-executed block.  The whole
 * predictor is one comparison of two addresses. */
static int predict_btfn(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s;
    return target < pc;
}

const BranchPredictor bp_btfn = {
    "btfn",
    "static: backward taken, forward not taken",
    reset_none,
    predict_btfn,
    update_none,
};
