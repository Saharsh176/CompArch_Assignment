CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -g -O0 \
           -Icore -Iisa -Iassembler -Iui -Ipipeline
LDFLAGS :=
BIN     := rvsim

SRCS := \
    core/cpu_state.c \
    core/memory.c \
    core/isa_registry.c \
    core/decoder.c \
    core/executor.c \
    isa/rv32i_base.c \
    assembler/assembler.c \
    ui/cli_display.c \
    pipeline/control.c \
    pipeline/hazard.c \
    pipeline/pipeline.c \
    main.c
OBJS := $(SRCS:.c=.o)
TESTS := tests/micro.s tests/forward_priority.s tests/sample1_sum_loop.s tests/sample2_mem_and_branches.s tests/sample3_hazards.s tests/sample4_data_hazards.s tests/load_use_cases.s tests/sample5_addi.s tests/sample6_rv32i_coverage.s tests/sample7_illegal_fault.s tests/sample8_memory_fault.s

.PHONY: all clean run memcheck test test-single test-pipeline verify verify-reference trace
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) $(PROG) --batch

memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch

# Optional cycle-by-cycle trace described in Lab Sheet 3, Section 9, Step 3.
rvsim-trace: $(SRCS)
	$(CC) $(CFLAGS) -DPIPELINE_TRACE -o $@ $(SRCS) $(LDFLAGS)

trace: rvsim-trace
	./rvsim-trace tests/micro.s --pipeline

test-single: $(BIN)
	@set -e; for f in $(TESTS); do echo "=== single-cycle: $$f ==="; ./$(BIN) "$$f" --batch >/dev/null; done
	@echo "All single-cycle smoke tests passed."

test-pipeline: $(BIN)
	@set -e; for f in $(TESTS); do \
	  echo "=== stalling pipeline: $$f ==="; ./$(BIN) "$$f" --pipeline >/dev/null; \
	  echo "=== forwarding pipeline: $$f ==="; ./$(BIN) "$$f" --pipeline-forward >/dev/null; \
	done
	@./$(BIN) tests/sample9_fetch_fault.s --pipeline --mem-size 4 >/dev/null
	@./$(BIN) tests/sample9_fetch_fault.s --pipeline-forward --mem-size 4 >/dev/null
	@echo "All pipeline smoke tests passed in both policies."

# Compare architectural register state (x0..x31), deliberately excluding PC/cycles.
verify: $(BIN)
	@set -e; tmp=$$(mktemp -d); trap 'rm -rf $$tmp' EXIT; \
	for f in $(TESTS); do \
	  name=$$(basename $$f .s); \
	  ./$(BIN) "$$f" --batch > $$tmp/$$name.single; \
	  ./$(BIN) "$$f" --pipeline > $$tmp/$$name.pipe; \
	  ./$(BIN) "$$f" --pipeline-forward > $$tmp/$$name.forward; \
	  grep '^x' $$tmp/$$name.single > $$tmp/$$name.single.regs; \
	  grep '^x' $$tmp/$$name.pipe > $$tmp/$$name.pipe.regs; \
	  grep '^x' $$tmp/$$name.forward > $$tmp/$$name.forward.regs; \
	  if ! diff -u $$tmp/$$name.single.regs $$tmp/$$name.pipe.regs || \
	     ! diff -u $$tmp/$$name.single.regs $$tmp/$$name.forward.regs; then \
	    echo "REGISTER MISMATCH: $$f"; exit 1; \
	  fi; \
	  if ! echo "sample7_illegal_fault.s sample8_memory_fault.s" | grep -q "$$name"; then \
	    for mode in pipe forward; do \
	      awk -F: '/Total Cycles/{gsub(/ /,"",$$2); c=$$2} /Instructions/{gsub(/ /,"",$$2); r=$$2} /Stalls/{gsub(/ /,"",$$2); s=$$2} /Flushes/{gsub(/ /,"",$$2); fl=$$2} END{if(c != r+s+fl+4) exit 1}' $$tmp/$$name.$$mode || { echo "CYCLE IDENTITY FAILED ($$mode): $$f"; exit 1; }; \
	    done; \
	  fi; \
	  echo "REGISTER MATCH (single/stall/forward): $$f"; \
	done; \
	./$(BIN) tests/sample9_fetch_fault.s --batch --mem-size 4 > $$tmp/fetch.single; \
	./$(BIN) tests/sample9_fetch_fault.s --pipeline --mem-size 4 > $$tmp/fetch.pipe; \
	grep '^x' $$tmp/fetch.single > $$tmp/fetch.single.regs; \
	grep '^x' $$tmp/fetch.pipe > $$tmp/fetch.pipe.regs; \
	diff -u $$tmp/fetch.single.regs $$tmp/fetch.pipe.regs; \
	grep -q 'status = MEM_FAULT' $$tmp/fetch.pipe; \
	./$(BIN) tests/sample7_illegal_fault.s --pipeline > $$tmp/illegal.pipe; \
	grep -q 'status = ILLEGAL_INSTRUCTION' $$tmp/illegal.pipe; \
	./$(BIN) tests/sample8_memory_fault.s --pipeline > $$tmp/memory.pipe; \
	grep -q 'status = MEM_FAULT' $$tmp/memory.pipe; \
	echo "Fault propagation tests passed."

verify-reference: $(BIN)
	@set -e; \
	check() { f="$$1"; mode="$$2"; expected="$$3"; out=$$(./$(BIN) "$$f" "$$mode"); \
	  echo "$$out" | grep -Fq "$$expected" || { echo "REFERENCE MISMATCH: $$f $$mode"; echo "$$out" | grep 'pipeline:'; exit 1; }; \
	  echo "REFERENCE MATCH: $$f $$mode"; }; \
	check tests/micro.s --pipeline 'pipeline: mode=stall cycles=15 retired=5 stalls=6 flushes=0 CPI=3.000 fwd_ex=0 fwd_mem=0 load_use=0'; \
	check tests/micro.s --pipeline-forward 'pipeline: mode=forward cycles=9 retired=5 stalls=0 flushes=0 CPI=1.800 fwd_ex=2 fwd_mem=1 load_use=0'; \
	check tests/sample1_sum_loop.s --pipeline 'pipeline: mode=stall cycles=108 retired=53 stalls=40 flushes=11 CPI=2.038 fwd_ex=0 fwd_mem=0 load_use=0'; \
	check tests/sample1_sum_loop.s --pipeline-forward 'pipeline: mode=forward cycles=68 retired=53 stalls=0 flushes=11 CPI=1.283 fwd_ex=13 fwd_mem=11 load_use=0'; \
	check tests/sample2_mem_and_branches.s --pipeline 'pipeline: mode=stall cycles=26 retired=13 stalls=8 flushes=1 CPI=2.000 fwd_ex=0 fwd_mem=0 load_use=0'; \
	check tests/sample2_mem_and_branches.s --pipeline-forward 'pipeline: mode=forward cycles=18 retired=13 stalls=0 flushes=1 CPI=1.385 fwd_ex=2 fwd_mem=2 load_use=0'; \
	check tests/sample4_data_hazards.s --pipeline 'pipeline: mode=stall cycles=69 retired=29 stalls=35 flushes=1 CPI=2.379 fwd_ex=0 fwd_mem=0 load_use=0'; \
	check tests/sample4_data_hazards.s --pipeline-forward 'pipeline: mode=forward cycles=36 retired=29 stalls=2 flushes=1 CPI=1.241 fwd_ex=9 fwd_mem=7 load_use=2'; \
	check tests/load_use_cases.s --pipeline-forward 'pipeline: mode=forward cycles=28 retired=19 stalls=4 flushes=1 CPI=1.474 fwd_ex=4 fwd_mem=5 load_use=4'

test: test-single test-pipeline verify verify-reference

clean:
	rm -f $(OBJS) $(BIN) rvsim-trace
