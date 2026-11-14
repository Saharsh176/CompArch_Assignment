#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "decoder.h"
#include "isa_registry.h"
#include "control.h"
#include "cpu_state.h"
#include "memory.h"
#include "branch_predictor.h"

/* Fault codes carried down the pipeline */
#define PF_NONE     0
#define PF_ILLEGAL  1
#define PF_FETCH    2
#define PF_MEM      3

/* Stage 1 -> Stage 2 Latch */
typedef struct {
    int valid;
    uint32_t pc;
    uint32_t raw;
    int fault;
    uint32_t pred_next;   /* address IF actually fetched next (the guess) */
} IFID;

/* Stage 2 -> Stage 3 Latch */
typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;
    uint32_t rs1_val;
    uint32_t rs2_val;
    uint32_t rs3_val;
    uint32_t sys_a0, sys_a7; /* captured ID operands for the no-hazard test model */
    uint32_t pred_next;   /* guess carried down to EX for comparison */
} IDEX;

/* Stage 3 -> Stage 4 Latch */
typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;
    uint32_t alu_result;
    uint32_t store_val;
    uint32_t sys_a0, sys_a7;
    uint32_t fault_addr;
    char fault_msg[128];
    int bkind;            /* Lab 4: 0 none, 1 cond branch, 2 jal, 3 jalr */
    int redir;            /* Lab 4: this instruction caused a redirect */
} EXMEM;

/* Stage 4 -> Stage 5 Latch */
typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;
    uint32_t wb_val;
    uint32_t sys_a0, sys_a7;
    uint32_t fault_addr;
    char fault_msg[128];
    int bkind;
    int redir;
} MEMWB;

/* Top-level Pipeline State and Statistics Counters */
typedef struct {
    IFID if_id;
    IDEX id_ex;
    EXMEM ex_mem;
    MEMWB mem_wb;
    uint64_t cycles;
    uint64_t retired;
    uint64_t stalls;
    uint64_t flushes;
    uint64_t load_use_stalls;
    uint64_t fwd_ex;
    uint64_t fwd_mem;
    int forwarding;
    int hazard_enabled;
    int sample_mode;             /* compatibility mode for supplied lab2/lab3 golden tests */
    int raw_mode;                /* Lab 2 no-hazard output convention */

    /* ---- Lab Sheet 4: branch prediction ---------------------------------
     * lab4 = 0 reproduces the Lab 3 machine exactly (--pipeline,
     * --pipeline-forward): no IF pre-decode, 1-cycle redirect.
     * lab4 = 1 is the Lab 4 machine: IF pre-decodes jal and asks the
     * predictor about conditional branches; EX compares guess vs. fact. */
    int lab4;
    int trace;                      /* print one line per cycle (--trace) */
    const BranchPredictor *bp;
    BPState bps;                    /* table owned by the pipeline */
    /* Control-flow counters are bumped at WB (retirement), so wrong-path
       instructions fetched past the final ecall can never be counted. */
    uint64_t branches;              /* conditional branches executed */
    uint64_t mispredicts;           /* ... whose guess was wrong */
    uint64_t jal_count;             /* redirected in IF: free */
    uint64_t jalr_count;            /* never predicted: 2 cycles each */
    uint64_t redirects;             /* mispredicts + jalr */
    uint64_t squashed;              /* wrong-path instructions discarded */
} Pipeline;

/* Function prototypes */
void pipeline_init(Pipeline *p, int forwarding);
/* Select the Lab 4 machine and its predictor (resets the predictor table). */
void pipeline_enable_lab4(Pipeline *p, const BranchPredictor *bp, int trace);
void pipeline_enable_sample(Pipeline *p, int hazard_enabled, int raw_mode);
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem);
void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles);
void pipeline_print_summary(const Pipeline *p);

#endif /* PIPELINE_STATE_H */


