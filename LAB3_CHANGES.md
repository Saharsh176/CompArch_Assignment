# Lab Sheet 3 — Data Hazards Implementation

This update implements the Lab Sheet 3 requirements on top of the Lab 2 project.

## Implemented

- Added `pipeline/hazard.h` and `pipeline/hazard.c`.
- Moved RAW dependency detection out of `pipeline.c` into `hazard_check()`.
- Preserved the Lab 2 stalling policy for `--pipeline`: it checks ID/EX, EX/MEM and MEM/WB, ignores invalid producers, x0 destinations and instructions that do not write a register, and performs the three required stall actions (hold IF/ID, inject an ID/EX bubble, freeze IF/PC).
- Added `--pipeline-forward` without removing `--pipeline`.
- Added EX/MEM-to-EX and MEM/WB-to-EX forwarding, with EX/MEM priority.
- Applied forwarded operands to ALU inputs, store data, branch comparisons and JALR targets.
- Added one-cycle load-use detection in forwarding mode using ID/EX.mem_read and the decoded source-use controls.
- Added `load_use_stalls`, `fwd_ex`, `fwd_mem` and `forwarding` to `Pipeline`.
- Added the required one-line pipeline summary with mode, cycles, retired, stalls, flushes, CPI and forwarding counters.
- Added `tests/micro.s` exactly following the five-instruction worked example in Section 4.
- Added `tests/forward_priority.s` from the Section 6 forwarding-priority example; it verifies EX/MEM takes priority over MEM/WB when both match a source.
- Added `tests/sample4_data_hazards.s` from Appendix A, covering EX/MEM and MEM/WB dependencies, x0, load-use, store-data forwarding and a taken branch.
- Added `tests/load_use_cases.s` to exercise all four load-use forms listed in Section 7: ALU operand, branch operand, store data (rs2), and load address base (rs1).
- Added optional cycle-by-cycle tracing in `pipeline/pipeline.c`, enabled with `make trace`, matching the debugging format in Section 9, Step 3.
- Updated the build and verification targets to compile the hazard unit, compare register dumps from single-cycle/stalling/forwarding modes, and assert the Section 9 reference counters.

## Files changed

- `pipeline/hazard.h` (new)
- `pipeline/hazard.c` (new)
- `pipeline/pipeline.c`
- `pipeline/pipeline_state.h`
- `main.c` (adds the `--pipeline-forward` option and selects the mode)
- `Makefile` (builds hazard.c and verifies both policies)
- `tests/micro.s` (new)
- `tests/sample4_data_hazards.s` (new; Appendix A)
- `tests/load_use_cases.s` (new; four Section 7 load-use forms)
- `Makefile` (`verify-reference` and optional `trace` targets)
- `README.md`

No files in `core/`, `isa/`, `assembler/` or `ui/` were modified.

## Verification performed

`make clean && make test` succeeds with GCC using `-Wall -Wextra -Wpedantic`. The test target now also asserts the Section 9 reference statistics and checks all four Section 7 load-use forms.

Reference checkpoints from Lab Sheet 3 reproduced:

| Program / mode | Cycles | Retired | Stalls | Flushes | CPI | fwd_ex | fwd_mem | load_use |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `micro.s`, stalling | 15 | 5 | 6 | 0 | 3.000 | 0 | 0 | 0 |
| `micro.s`, forwarding | 9 | 5 | 0 | 0 | 1.800 | 2 | 1 | 0 |
| `sample1_sum_loop.s`, stalling | 108 | 53 | 40 | 11 | 2.038 | 0 | 0 | 0 |
| `sample1_sum_loop.s`, forwarding | 68 | 53 | 0 | 11 | 1.283 | 13 | 11 | 0 |
| `sample2_mem_and_branches.s`, stalling | 26 | 13 | 8 | 1 | 2.000 | 0 | 0 | 0 |
| `sample2_mem_and_branches.s`, forwarding | 18 | 13 | 0 | 1 | 1.385 | 2 | 2 | 0 |
| `sample4_data_hazards.s`, stalling | 69 | 29 | 35 | 1 | 2.379 | 0 | 0 | 0 |
| `sample4_data_hazards.s`, forwarding | 36 | 29 | 2 | 1 | 1.241 | 9 | 7 | 2 |
| `load_use_cases.s`, forwarding | 28 | 19 | 4 | 1 | 1.474 | 4 | 5 | 4 |

For the existing `sample3_hazards.s`, forwarding reports one load-use stall; both modes match the single-cycle register file. The complete `make test` run also checks all included normal test programs in both pipeline modes, register equivalence, fault propagation, and the cycle accounting identity on non-faulting programs.

## Notes on the uploaded sheet

- Appendix A uses `la gp, data`, but this project's assembler does not implement the `la` pseudo-instruction. To keep `assembler/` untouched as required by Section 2, the test uses the equivalent two-instruction expansion `lui gp, 0` / `addi gp, gp, 120` for the data address at `0x78`. This preserves the two-instruction dependency and the Appendix A instruction count.
- The PDF contains inconsistent Appendix A aggregate figures. Section 9's verification table gives stalling = 69 cycles / 35 stalls and forwarding = 36 cycles / 2 stalls, which are reproduced by the program and satisfy the stated cycle identity. The Appendix A prose on page 12 instead says 62 cycles / 27 stalls and 37 cycles / 2 stalls; those values conflict with the Section 9 table and, for forwarding, with the cycle identity. The implementation follows the explicit Section 9 verification table.
- The H1-H12 suite is mentioned as separately handed out, but those twelve source files are not included in this 13-page PDF or the provided project. They have not been fabricated. `load_use_cases.s` directly covers the four load-use patterns explicitly listed in Section 7.
- The cycle-by-cycle trace in Section 9 is available through `make trace`; it is not enabled during normal builds or tests, so normal output remains concise.
