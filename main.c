#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "assembler.h"
#include "cli_display.h"
#include "pipeline_state.h"  

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  --batch              run to completion with no interaction (single-cycle), then dump state\n"
        "  --pipeline           run using the 5-stage pipeline simulator in batch mode\n"
        "  --max-cycles N       cycle limit for batch/pipeline mode (default %llu)\n"
        "  --mem-size N         memory size in bytes (default %u)\n",
        prog, (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int batch_mode = 0;
    int pipeline_mode = 0; /* <-- Added pipeline flag */
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size = DEFAULT_MEM_SIZE;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch_mode = 1;
        } else if (strcmp(argv[i], "--pipeline") == 0) {
            pipeline_mode = 1;
            batch_mode = 1; /* Pipeline mode runs in batch */
        } else if (strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--mem-size") == 0 && i + 1 < argc) {
            mem_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "unrecognized option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    isa_registry_init();
    register_rv32i_base();

    Memory mem;
    if (memory_init(&mem, mem_size) != 0) {
        fprintf(stderr, "error: could not allocate %u bytes of simulated memory\n", mem_size);
        return 1;
    }

    uint32_t entry_point = 0;
    AsmError err;
    AsmStatus asm_status = assemble_file(program_path, &mem, &entry_point, &err);
    if (asm_status != ASM_OK) {
        fprintf(stderr, "assembly error");
        if (err.line_number > 0) fprintf(stderr, " (line %d)", err.line_number);
        fprintf(stderr, ": %s\n", err.message);
        memory_free(&mem);
        return 1;
    }

    printf("assembled '%s' ok. entry point = 0x%08x\n", program_path, entry_point);

    CPUState cpu;
    cpu_reset(&cpu, entry_point);

    if (pipeline_mode) {
        /* Run 5-stage pipeline simulator */
        Pipeline pipeline;
        pipeline_init(&pipeline);
        printf("running with 5-stage pipeline simulator...\n");
        pipeline_run_batch(&pipeline, &cpu, &mem, max_cycles);
        
        printf("\n--- Pipeline Execution Summary ---\n");
        printf("Total Cycles   : %llu\n", (unsigned long long)pipeline.cycles);
        printf("Instructions   : %llu\n", (unsigned long long)pipeline.retired);
        printf("Stalls         : %llu\n", (unsigned long long)pipeline.stalls);
        printf("Flushes        : %llu\n", (unsigned long long)pipeline.flushes);
        if (pipeline.cycles > 0) {
            printf("CPI            : %.2f\n", (double)pipeline.cycles / (pipeline.retired > 0 ? pipeline.retired : 1));
        }
    } else if (batch_mode) {
        run_batch(&cpu, &mem, max_cycles);
    } else {
        run_repl(&cpu, &mem);
    }

    memory_free(&mem);
    return 0;
}