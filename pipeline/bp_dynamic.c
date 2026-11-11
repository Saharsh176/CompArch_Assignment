/* Tasks 2 and 3 -- the two dynamic predictors.
 * Both index BPState.table with bp_index(pc) = pc[7:2]; there is no tag,
 * so two branches 256 bytes apart silently share (and train) one entry. */
#include "branch_predictor.h"
#include <string.h>

/* ======================= Task 2: 1-bit last-outcome ==================== */
/* table[i] = 1 if the last branch that used entry i was taken. */
static void reset_1bit(BPState *s) {
    memset(s->table, 0, sizeof(s->table));      /* start: not taken */
}

static int predict_1bit(const BPState *s, uint32_t pc, uint32_t target) {
    (void)target;                               /* direction comes from the table */
    return s->table[bp_index(pc)];
}

static void update_1bit(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)target;
    s->table[bp_index(pc)] = taken ? 1 : 0;
}

const BranchPredictor bp_1bit = {
    "1bit",
    "dynamic: 1-bit last-outcome table (64 entries)",
    reset_1bit,
    predict_1bit,
    update_1bit,
};

/* =================== Task 3: 2-bit saturating counter ================== */
/*   0 strongly NT  1 weakly NT | 2 weakly T  3 strongly T                  */
#define CTR_WEAK_NOT_TAKEN 1
#define CTR_TAKEN_THRESHOLD 2
#define CTR_MAX 3

/* Start weakly-not-taken: costs one extra miss on the first execution of
 * each loop's backward branch, but never predicts "taken" for a branch the
 * predictor has not yet seen. */
static void reset_2bit(BPState *s) {
    memset(s->table, CTR_WEAK_NOT_TAKEN, sizeof(s->table));
}

static int predict_2bit(const BPState *s, uint32_t pc, uint32_t target) {
    (void)target;
    return s->table[bp_index(pc)] >= CTR_TAKEN_THRESHOLD;   /* the high bit */
}

static void update_2bit(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)target;
    uint8_t *c = &s->table[bp_index(pc)];
    if (taken) { if (*c < CTR_MAX) (*c)++; }    /* saturate, never wrap */
    else       { if (*c > 0)       (*c)--; }
}

const BranchPredictor bp_2bit = {
    "2bit",
    "dynamic: 2-bit saturating counters (64 entries, start weakly not taken)",
    reset_2bit,
    predict_2bit,
    update_2bit,
};
