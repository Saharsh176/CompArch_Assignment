# RV32 Simulator — Lab Sheets 2–5

This repository contains a single-cycle RV32I reference engine and a five-stage
pipeline extended with data-hazard handling, branch prediction, and RV32F
single-precision floating point.

## Build

Requires GCC (C11) and GNU Make.

```sh
make clean
make
```

The build uses `-Wall -Wextra -Wpedantic -frounding-math` and links `libm`.
The current build completes without compiler warnings.

## Run modes

```sh
# Single-cycle architectural oracle
./rvsim tests/f1_broad.s --batch --single-cycle

# Lab 4/5 pipeline: forwarding + 2-bit predictor by default
./rvsim tests/f1_broad.s --batch

# Select the Lab 4/5 hazard policy and branch predictor
./rvsim tests/f3_kernel.s --batch --hazard forward --predictor 2bit
./rvsim tests/f3_kernel.s --batch --hazard stall  --predictor btfn

# Lab 3 sheet's original modes (kept for its reference checkpoints)
./rvsim tests/sample1_sum_loop.s --pipeline
./rvsim tests/sample1_sum_loop.s --pipeline-forward

# Supplied Lab 2/3 sample-suite timing convention (no predictor, 2-cycle redirects)
./rvsim tests/p1_broad.s --pipeline-raw
./rvsim tests/h1_broad.s --pipeline-sample --hazard none
./rvsim tests/h1_broad.s --pipeline-sample --hazard stall
./rvsim tests/h1_broad.s --pipeline-sample --hazard forward
```

`--pipeline` and `--pipeline-forward` preserve the Lab 3 sheet's original
one-cycle redirect timing and remain separate from the Lab 4 predictor path.
`--pipeline-raw` and `--pipeline-sample` are compatibility modes for the
provided sample archive, whose Lab 2/3 goldens use a no-prediction,
two-cycle-redirect pipeline. The `none` policy intentionally demonstrates
incorrect results when dependencies are not handled.

The normal `--batch` mode uses the Lab 4/5 pipeline unless `--single-cycle` is
specified. The default predictor is `2bit`; available predictors are
`not-taken`, `btfn`, `1bit`, and `2bit`.

## Implemented functionality

### Lab 3 — data hazards

- Standalone hazard detection unit and unified source/destination metadata.
- Stalling-only policy retained.
- EX/MEM and MEM/WB forwarding with EX/MEM priority.
- Load-use interlock, including the third source operand used by FMA.
- Forwarding and stall counters.

### Lab 4 — branch prediction

- Always-not-taken and backward-taken/forward-not-taken static predictors.
- One-bit and two-bit saturating dynamic predictors.
- IF pre-decode for conditional branches and `jal`; `jalr` remains unpredicted.
- Mispredict, redirect, squashed-instruction, and accuracy counters.

### Lab 5 — RV32F

- Separate 32-register floating-point file (`f0`–`f31`) and `fcsr`.
- All 26 listed RV32F instructions: `flw`, `fsw`, sign injection, raw moves,
  classification, arithmetic, square root, fused multiply-adds, comparisons,
  min/max, and signed/unsigned integer conversions.
- Five rounding modes (`rne`, `rtz`, `rdn`, `rup`, `rmm`) and dynamic `frm`.
- Sticky `NV`, `DZ`, `OF`, `UF`, and `NX` flags, with CSR read/write support.
- Canonical NaN handling, signalling-NaN exceptions, signed-zero min/max,
  conversion saturation, and fused single-rounding operations.
- FP values use the same unified hazard and forwarding machinery as integer
  values; `flw`/`fsw` use the pipeline MEM stage.

The uploaded starting archive did not contain the Lab 5 infrastructure described
as pre-supplied in the sheet (FP state, extended registry metadata, FP decoder /
assembler support, CSR support, or FP-aware pipeline latches). Those missing
pieces were added to the corresponding core, ISA, assembler, and pipeline files
so the Lab 5 ISA implementation could run end to end. The single-cycle engine
remains the reference oracle.

## Tests

```sh
# Original project regression suite
make test

# Full supplied sample_tests.zip golden suite + extra edge/dependency tests
make test-extended
```

`tests/sample_tests/` contains the extracted supplied archive, including its
expected output files. The extended test runner checks:

- Lab 1 single-cycle sample cycle counts and output.
- Lab 2 single-cycle and no-hazard pipeline goldens.
- Lab 3 single-cycle, no-hazard, stall, and forwarding goldens.
- Lab 4 all four predictors against every supplied golden.
- Lab 5 single-cycle, forwarding, stall, register-file, and `fcsr` goldens.
- Additional tests for FP signed zero, qNaN/sNaN, exception flags, RMM ties,
  fused cancellation, `f0`, third-source forwarding, load-use, overwrite
  priority, invalid dynamic rounding mode, and branch paths.

`make test` also runs the original smoke, register-equivalence, fault, cycle
identity, and Lab 4 predictor checks. The test script allows the supplied
`b3_applied` stall-mode terminal drain case's one-cycle identity exception;
its forward-mode cycle reference remains exact.

## Important files

- `pipeline/hazard.[ch]`: RAW detection, load-use detection, and forwarding.
- `pipeline/branch_predictor.[ch]`, `pipeline/bp_static.c`,
  `pipeline/bp_dynamic.c`: branch predictor interface and implementations.
- `isa/rv32f.c`: RV32F instruction handlers and registrations.
- `isa/fp_helpers.[ch]`: rounding, IEEE flags, and bit/float helpers.
- `isa/zicsr.c`: CSR instructions for `fflags`, `frm`, and `fcsr`.
- `tests/run_extended_tests.sh`: supplied golden and custom test runner.
