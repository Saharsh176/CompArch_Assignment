#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make -s rvsim
checks=0
pass() { checks=$((checks + 1)); }
run_exact() {
  local expected="$1"; shift
  local tmp
  tmp=$(mktemp)
  ./rvsim "$@" >"$tmp"
  if ! diff -u "$expected" "$tmp"; then
    echo "FAILED: $*" >&2
    rm -f "$tmp"
    exit 1
  fi
  rm -f "$tmp"
  pass
}

# Lab 1: no golden files were supplied; check the documented cycle/print results.
for spec in 's1_alu 41 2016' 's2_memory 87 36' 's3_control 634 755120'; do
  read -r name cycles printed <<<"$spec"
  out=$(./rvsim "tests/sample_tests/lab1_skeleton/$name.s" --batch --single-cycle)
  grep -q "cycles = $cycles" <<<"$out"
  [[ "$out" == *"$printed"* ]]
  pass
 done

# Lab 2: oracle plus the no-hazard, two-cycle redirect pipeline.
for name in p1_broad p2_edge p3_kernel; do
  run_exact "tests/sample_tests/lab2_pipeline/expected/$name.single-cycle.out" "tests/$name.s" --batch --single-cycle
  run_exact "tests/sample_tests/lab2_pipeline/expected/$name.pipeline.out" "tests/$name.s" --pipeline-raw
 done

# Lab 3: oracle, intentionally wrong no-hazard mode, stalling and forwarding.
for name in h1_broad h2_edge h3_kernel; do
  run_exact "tests/sample_tests/lab3_hazards/expected/$name.single-cycle.out" "tests/$name.s" --batch --single-cycle
  run_exact "tests/sample_tests/lab3_hazards/expected/$name.none.out" "tests/$name.s" --pipeline-sample --hazard none
  run_exact "tests/sample_tests/lab3_hazards/expected/$name.stall.out" "tests/$name.s" --pipeline-sample --hazard stall
  run_exact "tests/sample_tests/lab3_hazards/expected/$name.forward.out" "tests/$name.s" --pipeline-sample --hazard forward
 done

# Lab 4: every static/dynamic predictor and the single-cycle oracle.
for name in b1_patterns b2_adversarial b3_applied; do
  run_exact "tests/sample_tests/lab4_branch/expected/$name.single-cycle.out" "tests/$name.s" --batch --single-cycle
  for predictor in not-taken btfn 1bit 2bit; do
    run_exact "tests/sample_tests/lab4_branch/expected/$name.$predictor.out" "tests/$name.s" --batch --predictor "$predictor"
  done
 done

# Lab 5: all 26 RV32F operations, fcsr flags and pipeline forwarding/stalls.
for name in f1_broad f2_special f3_kernel; do
  run_exact "tests/sample_tests/lab5_fp/expected/$name.single-cycle.out" "tests/$name.s" --batch --single-cycle
  run_exact "tests/sample_tests/lab5_fp/expected/$name.forward.out" "tests/$name.s" --batch --hazard forward
  if [[ -f "tests/sample_tests/lab5_fp/expected/$name.stall.out" ]]; then
    run_exact "tests/sample_tests/lab5_fp/expected/$name.stall.out" "tests/$name.s" --batch --hazard stall
  fi
 done

# Additional easy/tricky cases: forwarding priority, FP rs3, f0, NaNs, signed
# zeros, RMM ties, fused cancellation, invalid dynamic rm and branch paths.
for name in fp_edges fp_hazard_priority int_hazard_priority; do
  oracle=$(mktemp)
  ./rvsim "tests/custom/$name.s" --batch --single-cycle >"$oracle"
  for hazard in forward stall; do
    actual=$(mktemp)
    ./rvsim "tests/custom/$name.s" --batch --hazard "$hazard" >"$actual"
    diff -u <(grep '^x' "$oracle") <(grep '^x' "$actual")
    diff -u <(grep '^f[0-9]' "$oracle") <(grep '^f[0-9]' "$actual")
    diff -u <(grep '^fcsr' "$oracle") <(grep '^fcsr' "$actual")
    rm -f "$actual"
    pass
  done
  rm -f "$oracle"
 done

oracle=$(mktemp)
./rvsim tests/custom/branch_edge.s --batch --single-cycle >"$oracle"
for predictor in not-taken btfn 1bit 2bit; do
  actual=$(mktemp)
  ./rvsim tests/custom/branch_edge.s --batch --predictor "$predictor" >"$actual"
  diff -u <(grep '^x' "$oracle") <(grep '^x' "$actual")
  rm -f "$actual"
  pass
done
rm -f "$oracle"

single=$(./rvsim tests/custom/fp_invalid_rm.s --batch --single-cycle)
piped=$(./rvsim tests/custom/fp_invalid_rm.s --batch --hazard forward)
grep -q 'status = ILLEGAL_INSTRUCTION' <<<"$single"
grep -q 'status = ILLEGAL_INSTRUCTION' <<<"$piped"
grep -Fq 'illegal floating-point rounding mode' <<<"$single"
grep -Fq 'illegal floating-point rounding mode' <<<"$piped"
pass

echo "$checks extended golden/custom checks passed."
