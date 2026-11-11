#include "branch_predictor.h"
#include <string.h>

/* The predictor table: every selectable predictor, in presentation order. */
static const BranchPredictor *const predictors[] = {
    &bp_not_taken, &bp_btfn, &bp_1bit, &bp_2bit
};

int bp_count(void) {
    return (int)(sizeof(predictors) / sizeof(predictors[0]));
}

const BranchPredictor *bp_at(int i) {
    return (i >= 0 && i < bp_count()) ? predictors[i] : NULL;
}

const BranchPredictor *bp_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < bp_count(); i++)
        if (strcmp(predictors[i]->name, name) == 0) return predictors[i];
    return NULL;
}

const BranchPredictor *bp_default(void) { return &bp_2bit; }
