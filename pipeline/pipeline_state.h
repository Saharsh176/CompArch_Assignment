#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "decoder.h"
#include "isa_registry.h"
#include "control.h"
#include "cpu_state.h"
#include "memory.h"

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
    uint32_t fault_addr;
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
    uint32_t fault_addr;
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
} Pipeline;

/* Function prototypes */
void pipeline_init(Pipeline *p, int forwarding);
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem);
void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles);

#endif /* PIPELINE_STATE_H */