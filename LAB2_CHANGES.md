# Lab Sheet 2 implementation changes

## Source changes

### `pipeline/pipeline.c`
- Reworked the cycle function around the required snapshot/commit model.
- Implemented WB, MEM, EX, ID and IF using old latches as inputs and next latches as outputs.
- Implemented the three-latch, no-forwarding hazard interlock and freezes IF/ID while injecting an ID/EX bubble.
- Implemented all listed RV32I ALU operations, signed/unsigned comparisons, shift masking, LUI/AUIPC and effective-address generation.
- Implemented signed and unsigned byte/halfword loads, word loads, and byte/halfword/word stores.
- Implemented EX-stage branch/jump resolution and flushes.
- Propagated illegal, fetch and data-memory faults through the pipeline and made WB the point where faults become visible.
- Ensured a fault or terminating ECALL commits the current cycle and kills younger in-flight instructions.

### `pipeline/control.c`
- Uses the ISA registry's `InstrFormat` in one switch, matching the lab's control-signal table.
- Generates register-write, write-back-source, memory, branch/jump, system and source-usage signals.

### `pipeline/pipeline_state.h`
- Kept the four required latch structures and pipeline statistics.
- MEMWB carries the write-back value rather than retaining EX/MEM-only fields.

### `main.c`
- Keeps the single-cycle mode unchanged.
- Prints the register file and CPU status after pipeline execution, in addition to pipeline statistics.
- Reports pipeline cycle count in the status display.

### `Makefile`
- Adds separate single-cycle and pipeline test targets.
- Adds register-file equivalence checks and the cycle-accounting identity check.
- `make test` runs the full test suite.

## Added tests
- `sample6_rv32i_coverage.s`: broad RV32I and control-flow coverage.
- `sample7_illegal_fault.s`: illegal instruction.
- `sample8_memory_fault.s`: out-of-bounds data load.
- `sample9_fetch_fault.s`: out-of-bounds instruction fetch (4-byte memory).

## Verified results
- Clean build with `-Wall -Wextra -Wpedantic`: passed without warnings.
- Single-cycle smoke tests: passed.
- Pipeline smoke tests: passed.
- Final x0-x31 register comparison: passed for the included programs.
- Illegal-instruction, data-memory and fetch-fault pipeline termination: passed.
- Cycle identity: passed for normal terminating tests.
- Sample 1 matches the lab reference: 108 cycles, 53 retired, 40 stalls, 11 flushes, CPI 2.038.
- Sample 2 matches the lab reference: 26 cycles, 13 retired, 8 stalls, 1 flush, CPI 2.000.

## Known source-material limitation
The provided PDF references an Appendix A sample-3 program with 18 retired
instructions, 19 stalls and 3 flushes. The uploaded PDF's page 6 is blank, and
the ZIP's `sample3_hazards.s` has only 11 dynamic instructions. The exact
Appendix A program is therefore unavailable in the supplied files; the
existing test was not silently replaced with a fabricated reference program.
