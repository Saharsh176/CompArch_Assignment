#!/usr/bin/env bash
# Lab Sheet 4 regression: registers vs oracle, golden tables, identities.
# usage: ./check_lab4.sh      (exit status 0 = everything passed)
# Portable: works with the bash 3.2 that macOS ships.
BIN=./rvsim; fail=0; checks=0
bad(){ echo "FAIL: $*"; fail=$((fail+1)); }
ok(){ checks=$((checks+1)); }
field(){ grep -E "^$1 " "$2" | head -1 | sed -E 's/^[^=]*= *([0-9.]+).*/\1/'; }
cyc(){ grep -oE 'cycles *= *[0-9]+' "$1" | tail -1 | grep -oE '[0-9]+$'; }

# ---- Step 1: registers identical to the single-cycle oracle ----------------
for f in tests/*.s; do
  extra=""; [[ $f == *sample9_fetch_fault* ]] && extra="--mem-size 4"
  $BIN $f --batch --single-cycle $extra > ref.txt 2>&1
  for h in forward stall; do for p in not-taken btfn 1bit 2bit; do
    $BIN $f --batch --hazard $h --predictor $p $extra > got.txt 2>&1
    diff <(grep '^x' ref.txt) <(grep '^x' got.txt) >/dev/null || bad "registers $f $h/$p"
    ok
  done; done
done

# ---- Step 2: golden table (program predictor cycles mispredicts) ----------
# Lookup helpers (no associative arrays: macOS ships bash 3.2).
golden(){ case "$1,$2" in   # "cycles mispredicts"
 t1_counted_loop,not-taken) echo "59 9";; t1_counted_loop,btfn) echo "43 1";; t1_counted_loop,1bit) echo "45 2";; t1_counted_loop,2bit) echo "45 2";;
 t2_nested_loops,not-taken) echo "78 11";; t2_nested_loops,btfn) echo "66 5";; t2_nested_loops,1bit) echo "76 10";; t2_nested_loops,2bit) echo "70 7";;
 t3_alternating,not-taken)  echo "76 11";; t3_alternating,btfn)  echo "64 5";; t3_alternating,1bit)  echo "74 10";; t3_alternating,2bit)  echo "74 10";;
 t4_calls,not-taken) echo "278 2";; t4_calls,btfn) echo "278 2";; t4_calls,1bit) echo "280 3";; t4_calls,2bit) echo "278 2";;
esac; }
retired_of(){ case $1 in t1_counted_loop) echo 37;; t2_nested_loops) echo 52;; t3_alternating) echo 50;; t4_calls) echo 219;; esac; }
branches_of(){ case $1 in t1_counted_loop) echo 10;; t2_nested_loops) echo 16;; t3_alternating) echo 16;; t4_calls) echo 20;; esac; }
jal_of(){ case $1 in t1_counted_loop|t2_nested_loops) echo 0;; t3_alternating) echo 4;; t4_calls) echo 33;; esac; }
jalr_of(){ case $1 in t4_calls) echo 18;; *) echo 0;; esac; }
for t in t1_counted_loop t2_nested_loops t3_alternating t4_calls; do
  $BIN tests/$t.s --batch --single-cycle > ref.txt
  [ "$(cyc ref.txt)" = "$(retired_of $t)" ] || bad "oracle cycles $t"; ok
  for p in not-taken btfn 1bit 2bit; do
    $BIN tests/$t.s --batch --predictor $p > got.txt
    set -- $(golden $t $p); c=$(cyc got.txt); m=$(field mispredicts got.txt)
    [ "$c" = "$1" ] || bad "$t $p cycles=$c expected $1"; ok
    [ "$m" = "$2" ] || bad "$t $p mispredicts=$m expected $2"; ok
    [ "$(field retired got.txt)" = "$(retired_of $t)" ] || bad "$t $p retired"; ok
    [ "$(field branches got.txt)" = "$(branches_of $t)" ] || bad "$t $p branches"; ok
    [ "$(field jal got.txt)" = "$(jal_of $t)" ] || bad "$t $p jal"; ok
    [ "$(field jalr got.txt)" = "$(jalr_of $t)" ] || bad "$t $p jalr"; ok
  done
done

# ---- Step 3: the two identities, every non-faulting program, every mode ----
for f in tests/*.s; do
  n=$(basename $f .s); case $n in sample7*|sample8*|sample9*) continue;; esac
  for h in forward stall; do for p in not-taken btfn 1bit 2bit; do
    $BIN $f --batch --hazard $h --predictor $p > got.txt
    c=$(cyc got.txt); r=$(field retired got.txt); s=$(field stalls got.txt)
    m=$(field mispredicts got.txt); jr=$(field jalr got.txt)
    d=$(grep -E '^redirects' got.txt | sed -E 's/^redirects = ([0-9]+).*/\1/')
    [ "$c" -eq $((r+4+2*d+s)) ] || bad "cycle identity $n $h/$p: $c != $r+4+2*$d+$s"; ok
    [ "$d" -eq $((m+jr)) ] || bad "redirect identity $n $h/$p: $d != $m+$jr"; ok
  done; done
done

# ---- legacy Lab 3 modes must be untouched ----------------------------------
make -s verify-reference >/dev/null 2>&1 || bad "Lab 3 verify-reference regressed"; ok

echo "$checks checks run, $fail failed"
[ $fail -eq 0 ] && echo "LAB 4: ALL CHECKS PASSED"
exit $fail