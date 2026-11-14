#include "pipeline_state.h"
#include "control.h"
#include "hazard.h"
#include "branch_predictor.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

static uint32_t read_unified(const CPUState *c, uint32_t r) {
    return r >= 32 ? cpu_read_freg(c, (int)(r - 32)) : cpu_read_reg(c, (int)r);
}
static void write_unified(CPUState *c, uint32_t r, uint32_t v) {
    if (r >= 32) cpu_write_freg(c, (int)(r - 32), v);
    else cpu_write_reg(c, (int)r, v);
}

void pipeline_init(Pipeline *p, int forwarding) {
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
    p->hazard_enabled = 1;
    p->bp = &bp_not_taken;      /* Lab 3 behaviour unless lab4 is enabled */
}

void pipeline_enable_lab4(Pipeline *p, const BranchPredictor *bp, int trace) {
    p->lab4 = 1;
    p->trace = trace;
    p->bp = bp ? bp : bp_default();
    p->bp->reset(&p->bps);
}

void pipeline_enable_sample(Pipeline *p, int hazard_enabled, int raw_mode) {
    p->sample_mode = 1;
    p->hazard_enabled = hazard_enabled;
    p->raw_mode = raw_mode;
    p->lab4 = 0;
}

/* Lab 4 IF pre-decode helpers: only the opcode and immediate are read,
 * a few gates in hardware (stands in for a BTB that always hits). */
static int32_t imm_b(uint32_t r) {
    uint32_t v = ((r >> 31) << 12) | (((r >> 7) & 1u) << 11) |
                 (((r >> 25) & 0x3fu) << 5) | (((r >> 8) & 0xfu) << 1);
    return (int32_t)(v << 19) >> 19;
}
static int32_t imm_j(uint32_t r) {
    uint32_t v = ((r >> 31) << 20) | (((r >> 12) & 0xffu) << 12) |
                 (((r >> 20) & 1u) << 11) | (((r >> 21) & 0x3ffu) << 1);
    return (int32_t)(v << 11) >> 11;
}

static uint32_t alu(const DecodedInstr *i, const InstructionHandler *h,
                    uint32_t pc, uint32_t a, uint32_t b) {
    const uint32_t f3 = i->funct3;
    const uint32_t f7 = i->funct7;
    const InstrFormat fmt = h->format;
    const uint32_t shamt = ((fmt == FMT_I_SHAMT) ? (uint32_t)i->imm : b) & 0x1fu;

    if (i->opcode == 0x17u) return pc + (uint32_t)i->imm; /* auipc */
    if (fmt == FMT_U) return (uint32_t)i->imm;       /* lui */
    if (fmt == FMT_I_LOAD || fmt == FMT_S)
        return a + (uint32_t)i->imm;                 /* effective address */

    if (fmt == FMT_R) {
        switch (f3) {
            case 0: return f7 == 0x20u ? a - b : a + b;
            case 1: return a << shamt;
            case 2: return (int32_t)a < (int32_t)b;
            case 3: return a < b;
            case 4: return a ^ b;
            case 5: return f7 == 0x20u ? (uint32_t)((int32_t)a >> shamt) : a >> shamt;
            case 6: return a | b;
            case 7: return a & b;
            default: return 0;
        }
    }

    /* I-type arithmetic and shifts. The decoder has already sign-extended imm. */
    switch (f3) {
        case 0: return a + (uint32_t)i->imm;          /* addi */
        case 1: return a << shamt;                   /* slli */
        case 2: return (int32_t)a < i->imm;          /* slti */
        case 3: return a < (uint32_t)i->imm;         /* sltiu */
        case 4: return a ^ (uint32_t)i->imm;         /* xori */
        case 5: return f7 == 0x20u ? (uint32_t)((int32_t)a >> shamt) : a >> shamt;
        case 6: return a | (uint32_t)i->imm;         /* ori */
        case 7: return a & (uint32_t)i->imm;         /* andi */
        default: return 0;
    }
}

static void raise_pipeline_fault(CPUState *cpu, int fault, uint32_t addr,
                                 uint32_t raw, int has_handler, const char *message) {
    cpu->status = (fault == PF_ILLEGAL) ? CPU_ILLEGAL_INSTRUCTION : CPU_MEM_FAULT;
    cpu->fault_addr = addr;
    cpu->pc = addr;
    if (fault == PF_ILLEGAL && !has_handler) {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "unknown instruction 0x%08x at pc=0x%08x (no handler registered)", raw, addr);
    } else if (fault == PF_ILLEGAL && message && message[0]) {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg), "%s", message);
    } else if (fault == PF_ILLEGAL) {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "illegal instruction 0x%08x at pc=0x%08x", raw, addr);
    } else if (fault == PF_FETCH) {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "instruction fetch out of bounds at pc=0x%08x", addr);
    } else {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "data memory access out of bounds at address 0x%08x", addr);
    }
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING &&
        !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid && !p->mem_wb.valid)
        return cpu->status;

    /* All stages read p (old latches) and write nx (next latches). */
    Pipeline nx = *p;
    int stall = 0;
    int redirect = 0;
    uint32_t redirect_pc = 0;
    nx.cycles++;
    nx.if_id.valid = nx.id_ex.valid = nx.ex_mem.valid = nx.mem_wb.valid = 0;

    /* WB: faults become architecturally visible only here. */
    if (p->mem_wb.valid) {
        if (p->mem_wb.fault != PF_NONE) {
            raise_pipeline_fault(cpu, p->mem_wb.fault,
                                 p->mem_wb.fault_addr, p->mem_wb.instr.raw,
                                 p->mem_wb.handler != NULL, p->mem_wb.fault_msg);
            /* Kill younger work and commit this final clock tick. */
            nx.if_id.valid = nx.id_ex.valid = nx.ex_mem.valid = nx.mem_wb.valid = 0;
            *p = nx;
            return cpu->status;
        }

        if (p->mem_wb.ctrl.reg_write) {
            uint32_t value = p->mem_wb.wb_val;
            if (p->mem_wb.ctrl.wb_src == WB_PC4) value = p->mem_wb.pc + 4u;
            write_unified(cpu, p->mem_wb.ctrl.dst, value);
        }

        if (p->mem_wb.ctrl.is_system && p->mem_wb.handler && p->mem_wb.handler->execute) {
            uint32_t saved_pc = cpu->pc;
            uint32_t saved_a0 = cpu->regs[10], saved_a7 = cpu->regs[17];
            cpu->pc = p->mem_wb.pc;
            if (p->sample_mode && !p->hazard_enabled) {
                cpu->regs[10] = p->mem_wb.sys_a0;
                cpu->regs[17] = p->mem_wb.sys_a7;
            }
            p->mem_wb.handler->execute(cpu, mem, &p->mem_wb.instr);
            cpu->regs[10] = saved_a0;
            cpu->regs[17] = saved_a7;
            cpu->pc = saved_pc;
        }
        nx.retired++;
        if (p->lab4) {
            switch (p->mem_wb.bkind) {
                case 1: nx.branches++;   break;
                case 2: nx.jal_count++;  break;
                case 3: nx.jalr_count++; break;
                default: break;
            }
            if (p->mem_wb.redir) {
                nx.redirects++;
                nx.squashed += 2;                 /* the instructions in IF and ID */
                if (p->mem_wb.bkind == 1) nx.mispredicts++;
            }
        }
        if (cpu->status != CPU_RUNNING) {
            /* An ecall exit retires in order; expose its architectural next PC. */
            cpu->pc = p->mem_wb.pc;
            nx.if_id.valid = nx.id_ex.valid = nx.ex_mem.valid = nx.mem_wb.valid = 0;
            *p = nx;
            return cpu->status;
        }
    }

    /* MEM: perform loads/stores and carry faults to WB. */
    if (p->ex_mem.valid) {
        nx.mem_wb.valid = 1;
        nx.mem_wb.pc = p->ex_mem.pc;
        nx.mem_wb.fault = p->ex_mem.fault;
        nx.mem_wb.instr = p->ex_mem.instr;
        nx.mem_wb.handler = p->ex_mem.handler;
        nx.mem_wb.ctrl = p->ex_mem.ctrl;
        nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
        snprintf(nx.mem_wb.fault_msg, sizeof(nx.mem_wb.fault_msg), "%s", p->ex_mem.fault_msg);
        nx.mem_wb.bkind = p->ex_mem.bkind;
        nx.mem_wb.redir = p->ex_mem.redir;
        nx.mem_wb.wb_val = p->ex_mem.alu_result;
        nx.mem_wb.sys_a0 = p->ex_mem.sys_a0;
        nx.mem_wb.sys_a7 = p->ex_mem.sys_a7;

        if (p->ex_mem.fault == PF_NONE && p->ex_mem.ctrl.mem_read) {
            uint32_t data = 0;
            MemStatus ms = MEM_OK;
            switch (p->ex_mem.instr.funct3) {
                case 0: { uint8_t v = 0; ms = memory_read_byte(mem, p->ex_mem.alu_result, &v); data = (uint32_t)(int32_t)(int8_t)v; break; }
                case 1: { uint16_t v = 0; ms = memory_read_half(mem, p->ex_mem.alu_result, &v); data = (uint32_t)(int32_t)(int16_t)v; break; }
                case 2: ms = memory_read_word(mem, p->ex_mem.alu_result, &data); break;
                case 4: { uint8_t v = 0; ms = memory_read_byte(mem, p->ex_mem.alu_result, &v); data = v; break; }
                case 5: { uint16_t v = 0; ms = memory_read_half(mem, p->ex_mem.alu_result, &v); data = v; break; }
                default: ms = MEM_ERR_OUT_OF_BOUNDS; break;
            }
            if (ms != MEM_OK) {
                nx.mem_wb.fault = PF_MEM;
                nx.mem_wb.fault_addr = p->ex_mem.alu_result;
            } else nx.mem_wb.wb_val = data;
        } else if (p->ex_mem.fault == PF_NONE && p->ex_mem.ctrl.mem_write) {
            MemStatus ms;
            if (p->ex_mem.instr.funct3 == 0)
                ms = memory_write_byte(mem, p->ex_mem.alu_result, (uint8_t)p->ex_mem.store_val);
            else if (p->ex_mem.instr.funct3 == 1)
                ms = memory_write_half(mem, p->ex_mem.alu_result, (uint16_t)p->ex_mem.store_val);
            else
                ms = memory_write_word(mem, p->ex_mem.alu_result, p->ex_mem.store_val);
            if (ms != MEM_OK) {
                nx.mem_wb.fault = PF_MEM;
                nx.mem_wb.fault_addr = p->ex_mem.alu_result;
            }
        }
    }

    /* EX: ALU, branch comparison, and redirect generation. */
    if (p->id_ex.valid) {
        nx.ex_mem.valid = 1;
        nx.ex_mem.pc = p->id_ex.pc;
        nx.ex_mem.fault = p->id_ex.fault;
        nx.ex_mem.instr = p->id_ex.instr;
        nx.ex_mem.handler = p->id_ex.handler;
        nx.ex_mem.ctrl = p->id_ex.ctrl;
        nx.ex_mem.store_val = p->id_ex.rs2_val;
        nx.ex_mem.sys_a0 = p->id_ex.sys_a0;
        nx.ex_mem.sys_a7 = p->id_ex.sys_a7;
        nx.ex_mem.fault_addr = p->id_ex.pc;
        nx.ex_mem.bkind = 0;
        nx.ex_mem.redir = 0;

        if (p->id_ex.fault == PF_NONE) {
            if (p->id_ex.ctrl.rd_fp || p->id_ex.ctrl.rs2_fp ||
                p->id_ex.handler->opcode == 0x53u ||
                p->id_ex.handler->format == FMT_R4 ||
                p->id_ex.handler->format == FMT_CSR) cpu->fp_used = 1;
            Operands operands = forward_operands(p, &p->id_ex);
            uint32_t a = operands.a, b = operands.b;
            nx.fwd_ex += (uint64_t)operands.fwd_ex;
            nx.fwd_mem += (uint64_t)operands.fwd_mem;
            nx.ex_mem.store_val = b;
            uint32_t f3 = p->id_ex.instr.funct3;
            InstrFormat fmt = p->id_ex.handler->format;
            if (p->id_ex.ctrl.is_branch) {
                int taken = 0;
                switch (f3) {
                    case 0: taken = a == b; break;
                    case 1: taken = a != b; break;
                    case 4: taken = (int32_t)a < (int32_t)b; break;
                    case 5: taken = (int32_t)a >= (int32_t)b; break;
                    case 6: taken = a < b; break;
                    case 7: taken = a >= b; break;
                }
                if (!p->lab4) {
                    /* Lab 3 machine: every taken branch redirects. */
                    if (taken) { redirect = 1; redirect_pc = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm; }
                } else {
                    uint32_t target = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm;
                    uint32_t real_next = taken ? target : p->id_ex.pc + 4u;
                    nx.ex_mem.bkind = 1;
                    /* EX is the only place predictor state changes. */
                    p->bp->update(&nx.bps, p->id_ex.pc, target, taken);
                    if (real_next != p->id_ex.pred_next) {
                        nx.ex_mem.redir = 1;
                        redirect = 1;
                        redirect_pc = real_next;
                    }
                }
            } else if (p->id_ex.ctrl.is_jump) {
                uint32_t real_next = (fmt == FMT_J) ? p->id_ex.pc + (uint32_t)p->id_ex.instr.imm
                                                    : (a + (uint32_t)p->id_ex.instr.imm) & ~1u;
                if (!p->lab4) {
                    redirect = 1;
                    redirect_pc = real_next;
                } else {
                    nx.ex_mem.bkind = (fmt == FMT_J) ? 2 : 3;
                    /* jal: IF already went to the target -> no mismatch.
                       jalr: IF fetched pc+4 -> almost always a mismatch. */
                    if (real_next != p->id_ex.pred_next) {
                        nx.ex_mem.redir = 1;
                        redirect = 1;
                        redirect_pc = real_next;
                    }
                }
                nx.ex_mem.alu_result = p->id_ex.pc + 4u;
            } else if (p->id_ex.handler &&
                       (p->id_ex.handler->opcode == 0x53u ||
                        p->id_ex.handler->format == FMT_R4 ||
                        p->id_ex.handler->format == FMT_CSR)) {
                /* Extension instructions execute through their registered handler.
                   Operands are injected into a temporary architectural snapshot so
                   the handler sees forwarded values, while its destination is held
                   until WB. fcsr is deliberately committed in EX, in program order. */
                CPUState tmp = *cpu;
                tmp.pc = p->id_ex.pc;
                tmp.next_pc = p->id_ex.pc + 4u;
                if (p->id_ex.ctrl.uses_rs1) write_unified(&tmp, p->id_ex.ctrl.src1, a);
                if (p->id_ex.ctrl.uses_rs2) write_unified(&tmp, p->id_ex.ctrl.src2, b);
                if (p->id_ex.ctrl.uses_rs3) write_unified(&tmp, p->id_ex.ctrl.src3, operands.c);
                p->id_ex.handler->execute(&tmp, mem, (DecodedInstr *)&p->id_ex.instr);
                cpu->fcsr = tmp.fcsr;
                if (tmp.status == CPU_ILLEGAL_INSTRUCTION) {
                    nx.ex_mem.fault = PF_ILLEGAL;
                    nx.ex_mem.fault_addr = p->id_ex.pc;
                    snprintf(nx.ex_mem.fault_msg, sizeof(nx.ex_mem.fault_msg), "%s", tmp.fault_msg);
                } else if (p->id_ex.ctrl.reg_write) {
                    nx.ex_mem.alu_result = read_unified(&tmp, p->id_ex.ctrl.dst);
                }
            } else {
                nx.ex_mem.alu_result = alu(&p->id_ex.instr, p->id_ex.handler,
                                           p->id_ex.pc, a, b);
            }
        }
    }

    /* ID: decode and ask the standalone hazard unit whether to interlock. */
    HazardVerdict hz = {0, 0};
    if (p->hazard_enabled && p->if_id.valid && !redirect && p->if_id.fault == PF_NONE) {
        DecodedInstr candidate;
        const InstructionHandler *candidate_handler = NULL;
        if (decode_instruction(p->if_id.raw, &candidate, &candidate_handler) == DECODE_OK) {
            ControlSignals candidate_ctrl = control_decode(&candidate, candidate_handler);
            hz = hazard_check(p, &candidate, &candidate_ctrl);
            if (hz.stall) {
                stall = 1;
                nx.stalls++;
                if (hz.load_use) nx.load_use_stalls++;
            }
        }
    }

    if (stall) {
        nx.if_id = p->if_id;       /* freeze IF/ID */
        nx.id_ex.valid = 0;        /* inject bubble into ID/EX */
    } else if (p->if_id.valid && !redirect) {
        nx.id_ex.valid = 1;
        nx.id_ex.pc = p->if_id.pc;
        nx.id_ex.pred_next = p->if_id.pred_next;
        nx.id_ex.instr.raw = p->if_id.raw;
        nx.id_ex.fault = p->if_id.fault;
        if (p->if_id.fault == PF_FETCH) {
            nx.id_ex.fault = PF_FETCH;
            nx.id_ex.handler = NULL;
            nx.id_ex.ctrl = (ControlSignals){0};
        } else {
            DecodeStatus ds = decode_instruction(p->if_id.raw, &nx.id_ex.instr, &nx.id_ex.handler);
            if (ds != DECODE_OK) {
                nx.id_ex.fault = PF_ILLEGAL;
                nx.id_ex.handler = NULL;
                nx.id_ex.ctrl = (ControlSignals){0};
            } else {
                nx.id_ex.ctrl = control_decode(&nx.id_ex.instr, nx.id_ex.handler);
                nx.id_ex.rs1_val = nx.id_ex.ctrl.uses_rs1 ? read_unified(cpu, nx.id_ex.ctrl.src1) : 0;
                nx.id_ex.rs2_val = nx.id_ex.ctrl.uses_rs2 ? read_unified(cpu, nx.id_ex.ctrl.src2) : 0;
                nx.id_ex.rs3_val = nx.id_ex.ctrl.uses_rs3 ? read_unified(cpu, nx.id_ex.ctrl.src3) : 0;
                if (p->sample_mode && !p->hazard_enabled && nx.id_ex.ctrl.is_system) {
                    nx.id_ex.sys_a0 = cpu_read_reg(cpu, 10);
                    nx.id_ex.sys_a7 = cpu_read_reg(cpu, 17);
                }
            }
        }
    }

    /* A redirect kills the younger decoded/fetched instruction. */
    if (redirect) {
        nx.flushes++;
        if (p->sample_mode) { nx.redirects++; nx.squashed += 2; }
        nx.id_ex.valid = 0;
        nx.if_id.valid = 0;
        /* Lab 3 machine: target is fetched in this same tick (1-cycle loss). */
        if (!p->lab4) cpu->pc = redirect_pc;
    }

    /* IF: freeze on stall; otherwise fetch the guessed path. */
    int fetched = 0;
    uint32_t fetched_pc = 0;
    if (!stall && (!redirect || p->lab4 || !p->sample_mode) && cpu->status == CPU_RUNNING) {
        uint32_t fetch_pc = cpu->pc;
        uint32_t raw = 0;
        MemStatus ms = memory_read_word(mem, fetch_pc, &raw);
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        nx.if_id.raw = raw;
        nx.if_id.fault = (ms == MEM_OK) ? PF_NONE : PF_FETCH;

        uint32_t next = fetch_pc + 4u;
        if (p->lab4 && ms == MEM_OK) {
            uint32_t opcode = raw & 0x7fu;
            if (opcode == 0x63u) {                       /* conditional branch */
                uint32_t target = fetch_pc + (uint32_t)imm_b(raw);
                if (p->bp->predict(&p->bps, fetch_pc, target)) next = target;
            } else if (opcode == 0x6fu) {                /* jal: always right */
                next = fetch_pc + (uint32_t)imm_j(raw);
            }                                            /* jalr: not predicted */
        }
        nx.if_id.pred_next = next;
        cpu->pc = next;
        fetched = 1;
        fetched_pc = fetch_pc;
    }

    /* Lab 4 machine: the wrong-path instruction in IF was really fetched this
       cycle (that is the second lost cycle). Squash it and restart fetch. */
    if (redirect && p->lab4) {
        nx.if_id.valid = 0;
        cpu->pc = redirect_pc;
    }

    if (p->lab4 && p->trace) {
        char s_if[16], s_id[16], s_ex[16], s_mem[16], s_wb[16];
        #define TR(buf, v, pc) do { if (v) snprintf(buf, sizeof buf, "0x%08x", (unsigned)(pc)); \
                                    else strcpy(buf, "----------"); } while (0)
        TR(s_if, fetched, fetched_pc);
        TR(s_id, p->if_id.valid, p->if_id.pc);
        TR(s_ex, p->id_ex.valid, p->id_ex.pc);
        TR(s_mem, p->ex_mem.valid, p->ex_mem.pc);
        TR(s_wb, p->mem_wb.valid, p->mem_wb.pc);
        #undef TR
        printf("[cycle %llu] IF %s | ID %s | EX %s | MEM %s | WB %s\n",
               (unsigned long long)nx.cycles, s_if, s_id, s_ex, s_mem, s_wb);
    }

#ifdef PIPELINE_TRACE
    fprintf(stderr, "c%-3llu IF=%04x ID=%04x EX=%04x MEM=%04x WB=%04x %s%s\n",
            (unsigned long long)nx.cycles, nx.if_id.pc, p->if_id.pc,
            p->id_ex.pc, p->ex_mem.pc, p->mem_wb.pc,
            stall ? "STALL " : "", redirect ? "FLUSH" : "");
#endif
    *p = nx;
    return cpu->status;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (p->cycles < max_cycles) {
        if (cpu->status != CPU_RUNNING &&
            !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid && !p->mem_wb.valid)
            break;
        pipeline_cycle(p, cpu, mem);
        if (cpu->status != CPU_RUNNING &&
            !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid && !p->mem_wb.valid)
            break;
    }
    if (!p->lab4 && !p->sample_mode) {
        printf("pipeline: mode=%s cycles=%llu retired=%llu stalls=%llu flushes=%llu CPI=%.3f fwd_ex=%llu fwd_mem=%llu load_use=%llu\n",
               p->forwarding ? "forward" : "stall",
               (unsigned long long)p->cycles, (unsigned long long)p->retired,
               (unsigned long long)p->stalls, (unsigned long long)p->flushes,
               p->retired ? (double)p->cycles / (double)p->retired : 0.0,
               (unsigned long long)p->fwd_ex, (unsigned long long)p->fwd_mem,
               (unsigned long long)p->load_use_stalls);
        return;
    }
    /* Lab 4/5 statistics are printed by main after the architectural state. */
}

void pipeline_print_summary(const Pipeline *p) {
    if (p->sample_mode && p->raw_mode) {
        printf("pipeline        = 5-stage, no hazard handling\n");
    } else if (p->sample_mode && !p->hazard_enabled) {
        printf("pipeline        = 5-stage, hazard unit 'none'\n");
    } else if (p->sample_mode) {
        printf("pipeline        = 5-stage, hazard unit '%s'\n", p->forwarding ? "forward" : "stall");
    } else {
        printf("pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n",
               p->forwarding ? "forward" : "stall", p->bp->name);
    }
    printf("retired         = %llu\n", (unsigned long long)p->retired);
    printf("CPI             = %.3f\n", p->retired ? (double)p->cycles / (double)p->retired : 0.0);
    if (!p->sample_mode || !p->raw_mode) {
        printf("stalls          = %llu\n", (unsigned long long)p->stalls);
        printf("forwards        = %llu\n", (unsigned long long)(p->fwd_ex + p->fwd_mem));
    }
    if (!p->sample_mode) {
        printf("branches        = %llu\n", (unsigned long long)p->branches);
        printf("mispredicts     = %llu\n", (unsigned long long)p->mispredicts);
        printf("accuracy        = %.1f%%\n", p->branches ?
               100.0 * (double)(p->branches - p->mispredicts) / (double)p->branches : 100.0);
        printf("jal             = %llu\n", (unsigned long long)p->jal_count);
        printf("jalr            = %llu\n", (unsigned long long)p->jalr_count);
    }
    printf("redirects       = %llu\n", (unsigned long long)(p->sample_mode ? p->redirects : p->redirects));
    printf("squashed        = %llu\n", (unsigned long long)p->squashed);
}
