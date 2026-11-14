# Lab Sheet 5 — RV32F implementation notes

## ISA and architectural state

- Added `fregs[32]`, `fcsr`, and FP register accessors to `CPUState`.
- Added registry operand metadata (`IF_RD_FP`, `IF_RS1_FP`, `IF_RS2_FP`,
  `IF_RS3_FP`, `IF_HAS_RM`, `IF_RS2_FIXED`) and extended formats `FMT_R2`,
  `FMT_R4`, and `FMT_CSR`.
- Extended decoding for the FMA third source, fixed `rs2` fields, and CSR
  numbers.
- Extended assembly parsing for FP ABI registers, rounding-mode operands,
  `.float`, `la`, sign/move pseudos, and the `frflags`/`fsflags`/`frrm`/`fsrm`
  aliases.

## Instructions

`isa/rv32f.c` registers the 26 instructions listed in the sheet:

- Memory and bit manipulation: `flw`, `fsw`, `fsgnj.s`, `fsgnjn.s`,
  `fsgnjx.s`, `fmv.x.w`, `fmv.w.x`, `fclass.s`.
- Arithmetic: `fadd.s`, `fsub.s`, `fmul.s`, `fdiv.s`, `fsqrt.s`, `fmadd.s`,
  `fmsub.s`, `fnmsub.s`, `fnmadd.s`.
- Comparisons / min-max / conversions: `feq.s`, `flt.s`, `fle.s`, `fmin.s`,
  `fmax.s`, `fcvt.w.s`, `fcvt.wu.s`, `fcvt.s.w`, `fcvt.s.wu`.

`isa/fp_helpers.c` resolves static/dynamic rounding modes, implements the
RMM tie-away rule, translates host floating-point exceptions into RISC-V
`fflags`, and canonicalizes arithmetic NaNs. `isa/zicsr.c` implements the six
CSR instructions for the FP status CSRs.

## Pipeline integration

FP operands are assigned unified register numbers (`x1..x31` = 1..31,
`f0..f31` = 32..63, and `x0` = 0). The hazard and forwarding units therefore
compare integer and FP dependencies through the same metadata. FMA's `rs3` is
included in load-use detection and forwarding. `flw` and `fsw` travel through
MEM like integer loads/stores. Registered FP/CSR handlers execute in EX against
a temporary CPU snapshot populated with the forwarded operands; the result is
held for WB and `fcsr` side effects are committed in order.

## Verification

`make test-extended` compares all available Lab 2–5 sample golden outputs. The
three Lab 5 sample programs match their expected single-cycle, forwarding, and
(where supplied) stalling outputs exactly, including all integer/FP registers
and `fcsr`. Extra custom tests compare the oracle against both pipeline hazard
policies and cover invalid dynamic rounding modes and fused cancellation.
