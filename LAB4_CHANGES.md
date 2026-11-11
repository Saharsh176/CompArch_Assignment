# Lab Sheet 4 — Branch Prediction: what changed, how, and why

## 0. Read this first: the sheet's "given" files were not in the upload
Sheet 4 says `branch_predictor.{h,c}`, the IF pre-decode / predictor wiring in
`pipeline.c`, the `--predictor/--hazard/--single-cycle/--trace` options and
tests `t1`–`t4` are **given**. The upload was the Lab 3 project and contained
none of them. I therefore built that scaffolding too, keeping it clearly
separate from the three files the sheet makes yours:

| File | Status | Notes |
|---|---|---|
| `pipeline/bp_static.c` | **Task 1 (yours)** | not-taken, btfn |
| `pipeline/bp_dynamic.c` | **Tasks 2+3 (yours)** | 1bit, 2bit |
| `pipeline/branch_predictor.h/.c` | scaffolding | interface exactly as printed in the sheet; `bp_find()` |
| `pipeline/pipeline.c`, `pipeline_state.h` | scaffolding | IF pre-decode, guess carried down the pipe, EX compare/update, counters, `--trace` |
| `main.c` | scaffolding | new options |
| `tests/t1..t4*.s` | scaffolding | written to reproduce the sheet's golden numbers |
| `check_lab4.sh`, `Makefile` | tooling | `make test-lab4`; header-dependency tracking |

If your course supplies the real "given" files, drop in `bp_static.c` and
`bp_dynamic.c` — they only depend on `branch_predictor.h` as printed in the sheet.
`core/ isa/ assembler/ ui/` are untouched.

## 1. How it works
* **IF** reads the fetched word's opcode. `jal` → fetch continues at `pc+imm` (free).
  Conditional branch → `bp->predict(&state, pc, target)` picks `target` or `pc+4`.
  Everything else, including `jalr`, → `pc+4`. The address chosen travels down the
  pipe as `pred_next`.
* **EX** computes the real next PC. For a branch it calls `bp->update(state, pc, target, taken)`
  (the only place predictor state changes). If real next ≠ `pred_next` it redirects.
* **Cost = 2 cycles**: the instruction in ID is squashed *and* the one IF fetched
  on the wrong path in that same cycle is squashed; fetch restarts the next cycle.
* **Counters are bumped at WB (retirement)**, not EX — see §4 for why.
* `--batch` now runs the predicting pipeline (default `--hazard forward --predictor 2bit`);
  `--batch --single-cycle` is the oracle. `--pipeline` / `--pipeline-forward` still run the
  **unchanged Lab 3 machine** (`lab4 = 0`), so every Lab 3 reference figure still passes.

## 2. Verification (all green)
`make test` → Lab 3 suite + `./check_lab4.sh`: **437 checks, 0 failed**:
* registers identical to the single-cycle oracle for all 15 programs × 4 predictors × 2 hazard policies;
* every cycles / mispredicts / retired / branches / jal / jalr figure in Sheet 4's tables;
* `cycles = retired + 4 + 2·redirects + stalls` and `redirects = mispredicts + jalr` on every
  non-faulting program in all 8 configurations;
* `--trace` on `t1` reproduces the sheet's cycles 7, 8, 11, 12, 13 character for character;
* build is `-Wall -Wextra -Wpedantic` clean.

## 3. Results (identical to the sheet)
| Program | Instrs | not-taken | btfn | 1bit | 2bit | oracle |
|---|---|---|---|---|---|---|
| t1_counted_loop | 37 | 59 | 43 | 45 | 45 | 37 |
| t2_nested_loops | 52 | 78 | 66 | 76 | 70 | 52 |
| t3_alternating | 50 | 76 | 64 | 74 | 74 | 50 |
| t4_calls | 219 | 278 | 278 | 280 | 278 | 219 |
| **total** | 358 | 491 | 451 | 475 | 467 | 358 |
| CPI | | 1.372 | 1.260 | 1.327 | 1.304 | 1.000 |
| mispredicts / 62 branches | | 33 | 13 | 25 | 21 | |
| accuracy | | 46.8% | 79.0% | 59.7% | 66.1% | |

### Gap to the floor (one sentence each)
Floor = unforeseeable outcomes only: t1 = 1 (loop exit), t2 = 5 (4 inner exits + outer exit),
t3 = 5 (loop exit + 4 of the alternating branch under any single-branch-history rule), t4 = 2
(the two taken forward branches). Plus `2 × jalr` cycles no direction predictor can touch (t4: 36).
* **not-taken** — never guesses "taken", so it pays for every taken branch (t1 9 vs 1).
* **btfn** — reaches the floor on all four: the address comparison already encodes "loops go backward".
* **1bit** — pays one warm-up miss per branch plus a re-entry miss after every loop exit (t2: 10 vs 5; t1: 2 vs 1).
* **2bit** — pays the warm-up miss but hysteresis removes the re-entry miss (t2: 7 vs 5); on t3 it
  oscillates between the two weak states and is wrong on every alternation (10 vs 5).

## 4. Things worth knowing (and putting in the report)
1. **t4 −66 cycles before any prediction**: 344 (Lab 2 figure) → 278 is exactly 2 × 33 `jal`, the IF
   pre-decode, not the predictor.
2. **btfn wins with no state**: the programs are short, so dynamic predictors never amortise their
   per-branch warm-up miss.
3. **2bit vs 1bit**: wins on t2 (7 vs 10, hysteresis across loop re-entry), ties on t3 (10 vs 10,
   neither can represent T N T N).
4. **Aliasing**: `bp_index = pc[7:2]` gives 64 entries and there is no tag, so branches 256 B apart
   share an entry and train each other (one loop's exit can make another mispredict). None of the four
   programs spans 256 B, so it cannot be observed here — only reasoned about.
5. **2-bit start value = 1 (weakly not-taken)**: never predicts taken for an unseen branch, costs one
   miss per loop. Alternative start = 2, measured: t1 45→43 (1 miss), t2 70→66, t3 74→**64** (better, 5 miss),
   t4 278→**284** (worse). *The sheet says t3 gets worse; with the alternating branch starting
   on T, mine gets better — t4 is the one that gets worse.*
6. **Why counters are bumped at retirement.** IF keeps fetching past the final `ecall`; those
   never-executed instructions reach EX one cycle before the pipeline drains. Counting in EX made
   `t4` report 21 branches and a phantom stall (cycles off by one from the identity). Counting at WB
   makes the statistics exact by construction. If you add a test program, avoid a `lw`/use pair
   right after the final `ecall` (the t4 layout does, with a comment).
7. **Lab 3 vs Lab 4 flush cost.** The Lab 3 machine redirects in the same tick (1 cycle per taken
   branch); Sheet 4's model is 2. I kept both: Lab 3 flags reproduce Lab 3 numbers, the new path
   uses 2. The sheet's "Lab 2" column (e.g. t4 = 344) is from a 2-cycle lineage, so the Lab 3 flags
   will print different numbers for those programs (t1 → 50, not 59).
8. Makefile gained `-MMD` header tracking. Before it, editing `pipeline_state.h` left stale objects
   with a different `Pipeline` layout (this produced a bogus failure and a "stack smashing" abort while developing).

## 5. Usage
```
./rvsim prog.s --batch --predictor {not-taken|btfn|1bit|2bit} [--hazard forward|stall] [--trace]
./rvsim prog.s --batch --single-cycle        # oracle
make test                                    # everything
```
