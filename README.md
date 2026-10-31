# RV32I Simulator — Lab Sheet 2: Five-Stage Pipeline

This project contains the original single-cycle RV32I simulator and a five-stage
pipelined implementation for the W7 RISC-V Pipeline Lab Sheet.

## Architecture

The original single-cycle execution path in `core/` remains available as the
reference implementation. The pipeline is implemented in `pipeline/` and uses
four latches:

- `IFID`: fetched instruction, PC, validity and fetch-fault state.
- `IDEX`: decoded instruction, handler, control signals and captured operands.
- `EXMEM`: ALU result, store value and memory/fault information.
- `MEMWB`: write-back value and control information.

`pipeline_cycle()` snapshots the old pipeline state into `nx`, has every stage
read the old latches and write the next latches, then commits the state once at
the end of the cycle. Stages are WB, MEM, EX, ID and IF. Control signals are
generated in ID. The hazard unit uses the lab's conservative three-latch
interlock (ID/EX, EX/MEM and MEM/WB); there is no forwarding. Branches and jumps
are resolved in EX and redirect the fetch PC while flushing younger work.
Faults are carried to WB, where they become visible as CPU status values.

## Build

Requires GCC (C11) and GNU Make. From the project root:

```sh
make clean
make
```

The build uses `-Wall -Wextra -Wpedantic` and should complete without warnings.

## Run

```sh
# Original single-cycle reference engine
./rvsim tests/sample1_sum_loop.s --batch

# Five-stage pipeline
./rvsim tests/sample1_sum_loop.s --pipeline

# Optional cycle and memory limits
./rvsim tests/sample1_sum_loop.s --pipeline --max-cycles 100000
./rvsim tests/sample9_fetch_fault.s --pipeline --mem-size 4
```

Both modes print the architectural register file and CPU status. Pipeline mode
also prints cycles, retired instructions, stalls, flushes and CPI.

## Tests

```sh
make test-single     # all normal/fault single-cycle programs
make test-pipeline   # all normal/fault pipeline programs, plus fetch-fault test
make verify          # compare x0..x31 between both engines and check cycle identity
make test            # run all of the above
```

The verification target excludes PC and cycle count from register comparison:
the pipeline fetch PC legitimately runs ahead. For normal terminating programs,
it also checks the lab's accounting identity:

```text
cycles = retired + stalls + flushes + 4
```

Test programs:

- `sample1_sum_loop.s`: loop, arithmetic, branch, jump, ECALL; expected print is 55.
- `sample2_mem_and_branches.s`: byte store, signed/unsigned byte loads and branch.
- `sample3_hazards.s`: load-use dependency and jump/branch behaviour.
- `sample5_addi.s`: independent ADDI sequence.
- `sample6_rv32i_coverage.s`: directed arithmetic, shift, U-type, load/store, branch, JAL and JALR coverage.
- `sample7_illegal_fault.s`: illegal-instruction fault propagation.
- `sample8_memory_fault.s`: data-memory fault propagation.
- `sample9_fetch_fault.s`: instruction-fetch fault propagation (run with `--mem-size 4`).

## Reference figures and sample-3 note

With the conservative three-latch interlock and EX-stage branch resolution,
`sample1_sum_loop.s` produces 108 cycles, 53 retired instructions, 40 stalls,
11 flushes and CPI 2.038. `sample2_mem_and_branches.s` produces 26 cycles,
13 retired instructions, 8 stalls, 1 flush and CPI 2.000. These match the
lab-sheet reference table.

The ZIP's `sample3_hazards.s` executes 11 dynamic instructions, not the 18
listed for sample 3 in the lab-sheet reference table. The uploaded PDF refers
to an Appendix A version of that test, but the supplied PDF page 6 is blank
and the ZIP does not contain that 18-instruction version. Therefore its exact
19-stall/3-flush reference row cannot be verified from the supplied materials.
The existing sample is retained rather than inventing an alleged Appendix A
program. Its architectural registers are still compared against the reference
engine, and the cycle-accounting identity is checked.

## Scope

The implementation supports the RV32I instruction subset listed in the lab
sheet: R-type arithmetic/logical/shifts, I-type arithmetic/logical/shifts,
LUI/AUIPC, byte/halfword/word loads and stores, all six conditional branches,
JAL, JALR and ECALL. It does not implement forwarding, branch prediction or
extensions outside RV32I; these are not required by this lab sheet.

## Lab Sheet 3: data hazards and forwarding

The original Lab 2 stalling pipeline remains available:

```bash
./rvsim tests/sample1_sum_loop.s --pipeline
```

The Lab 3 forwarding pipeline is available separately:

```bash
./rvsim tests/sample1_sum_loop.s --pipeline-forward
```

Run the worked five-instruction comparison and the automated checks with:

```bash
./rvsim tests/micro.s --pipeline
./rvsim tests/micro.s --pipeline-forward
make test
```

`--pipeline` retains the Lab 2 three-latch stalling policy. `--pipeline-forward` forwards from EX/MEM and MEM/WB and inserts one stall for a detected load-use dependency. See `LAB3_CHANGES.md` for implementation details and reference results. The Lab Sheet 3 PDF supplied with this project references an Appendix A `sample4_data_hazards.s` listing that is not present in that PDF; its exact reference program must be supplied before its table row can be reproduced.


## Lab Sheet 3: Data Hazards and Forwarding

Build and run all tests:

```bash
make clean
make test
```

Run the Appendix A hazard program in both policies:

```bash
./rvsim tests/sample4_data_hazards.s --pipeline
./rvsim tests/sample4_data_hazards.s --pipeline-forward
```

Run the four targeted load-use cases from Section 7:

```bash
./rvsim tests/load_use_cases.s --pipeline-forward
```

Show the cycle-by-cycle trace for the worked example in Section 4:

```bash
make trace
```

`make test` includes register-file equivalence checks, fault checks, cycle accounting, and the reference-statistics assertions from Section 9. See `LAB3_CHANGES.md` and `TEST_RESULTS_LAB3.txt` for implementation details and the PDF's inconsistent Appendix A aggregate figures.
