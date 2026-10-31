#include "pipeline_state.h"
#include "control.h"
#include "hazard.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

void pipeline_init(Pipeline *p, int forwarding) {
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
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

static void raise_pipeline_fault(CPUState *cpu, int fault, uint32_t addr) {
    cpu->status = (fault == PF_ILLEGAL) ? CPU_ILLEGAL_INSTRUCTION : CPU_MEM_FAULT;
    cpu->fault_addr = addr;
    if (fault == PF_ILLEGAL) {
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                 "unknown instruction at pc=0x%08x", addr);
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
                                 p->mem_wb.fault_addr);
            /* Kill younger work and commit this final clock tick. */
            nx.if_id.valid = nx.id_ex.valid = nx.ex_mem.valid = nx.mem_wb.valid = 0;
            *p = nx;
            return cpu->status;
        }

        if (p->mem_wb.ctrl.reg_write) {
            uint32_t value = p->mem_wb.wb_val;
            if (p->mem_wb.ctrl.wb_src == WB_PC4) value = p->mem_wb.pc + 4u;
            cpu_write_reg(cpu, (int)p->mem_wb.instr.rd, value);
        }

        if (p->mem_wb.ctrl.is_system && p->mem_wb.handler && p->mem_wb.handler->execute) {
            uint32_t saved_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, &p->mem_wb.instr);
            cpu->pc = saved_pc;
        }
        nx.retired++;
        if (cpu->status != CPU_RUNNING) {
            /* An ecall exit retires in order; younger prefetched instructions die. */
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
        nx.mem_wb.wb_val = p->ex_mem.alu_result;

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
        nx.ex_mem.fault_addr = p->id_ex.pc;

        if (p->id_ex.fault == PF_NONE) {
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
                if (taken) { redirect = 1; redirect_pc = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm; }
            } else if (p->id_ex.ctrl.is_jump) {
                redirect = 1;
                redirect_pc = (fmt == FMT_J) ? p->id_ex.pc + (uint32_t)p->id_ex.instr.imm
                                             : (a + (uint32_t)p->id_ex.instr.imm) & ~1u;
                nx.ex_mem.alu_result = p->id_ex.pc + 4u;
            } else {
                nx.ex_mem.alu_result = alu(&p->id_ex.instr, p->id_ex.handler,
                                           p->id_ex.pc, a, b);
            }
        }
    }

    /* ID: decode and ask the standalone hazard unit whether to interlock. */
    HazardVerdict hz = {0, 0};
    if (p->if_id.valid && !redirect && p->if_id.fault == PF_NONE) {
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
                nx.id_ex.rs1_val = cpu_read_reg(cpu, (int)nx.id_ex.instr.rs1);
                nx.id_ex.rs2_val = cpu_read_reg(cpu, (int)nx.id_ex.instr.rs2);
            }
        }
    }

    /* A redirect kills the younger decoded/fetched instruction. Count one
       lost control-flow cycle, then fetch the target in this same tick. */
    if (redirect) {
        nx.flushes++;
        nx.id_ex.valid = 0;
        nx.if_id.valid = 0;
        cpu->pc = redirect_pc;
    }

    /* IF: freeze on stall; otherwise fetch sequentially or from redirect. */
    if (!stall && cpu->status == CPU_RUNNING) {
        uint32_t fetch_pc = cpu->pc;
        uint32_t raw = 0;
        MemStatus ms = memory_read_word(mem, fetch_pc, &raw);
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        nx.if_id.raw = raw;
        nx.if_id.fault = (ms == MEM_OK) ? PF_NONE : PF_FETCH;
        cpu->pc = fetch_pc + 4u;
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
    printf("pipeline: mode=%s cycles=%llu retired=%llu stalls=%llu flushes=%llu CPI=%.3f fwd_ex=%llu fwd_mem=%llu load_use=%llu\n",
           p->forwarding ? "forward" : "stall",
           (unsigned long long)p->cycles, (unsigned long long)p->retired,
           (unsigned long long)p->stalls, (unsigned long long)p->flushes,
           p->retired ? (double)p->cycles / (double)p->retired : 0.0,
           (unsigned long long)p->fwd_ex, (unsigned long long)p->fwd_mem,
           (unsigned long long)p->load_use_stalls);
}
