#include "pipeline_state.h"
#include "control.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(Pipeline));
}

static uint32_t alu(const DecodedInstr *i, const InstructionHandler *handler, uint32_t pc, uint32_t a, uint32_t b) {
    uint32_t funct3 = i->funct3;
    uint32_t funct7 = i->funct7;
    InstrFormat fmt = handler ? handler->format : FMT_R;

    (void)pc;

    if (fmt == FMT_U) {
        return i->imm;
    }
    if (i->opcode == 0x17) { /* auipc */
        return pc + i->imm;
    }
    if (fmt == FMT_I_LOAD || fmt == FMT_S) {
        return a + i->imm;
    }

    if (fmt == FMT_R) {
        if (funct3 == 0) return (funct7 == 0x20) ? (a - b) : (a + b);
        if (funct3 == 1) return a << (b & 0x1F);
        if (funct3 == 2) return ((int32_t)a < (int32_t)b) ? 1 : 0;
        if (funct3 == 3) return (a < b) ? 1 : 0;
        if (funct3 == 4) return a ^ b;
        if (funct3 == 5) return (funct7 == 0x20) ? ((int32_t)a >> (b & 0x1F)) : (a >> (b & 0x1F));
        if (funct3 == 6) return a | b;
        if (funct3 == 7) return a & b;
    } else { /* I-type arithmetic / shift */
        uint32_t shamt = i->imm & 0x1F;
        if (funct3 == 0) return a + i->imm;
        if (funct3 == 1) return a << shamt;
        if (funct3 == 2) return ((int32_t)a < (int32_t)i->imm) ? 1 : 0;
        if (funct3 == 3) return (a < (uint32_t)i->imm) ? 1 : 0;
        if (funct3 == 4) return a ^ i->imm;
        if (funct3 == 5) return (funct7 == 0x20) ? ((int32_t)a >> shamt) : (a >> shamt);
        if (funct3 == 6) return a | i->imm;
        if (funct3 == 7) return a & i->imm;
    }
    return 0;
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING && 
        !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid && !p->mem_wb.valid) {
        return cpu->status;
    }

    Pipeline nx = *p;
    int stall = 0;
    int do_redirect = 0;

    nx.cycles++; /* Real cycle increment */

    nx.mem_wb.valid = 0;
    nx.ex_mem.valid = 0;
    nx.id_ex.valid = 0;
    nx.if_id.valid = 0;

    /* =========================================================================
     * 1. WRITE BACK (WB)
     * ========================================================================= */
    if (p->mem_wb.valid) {
        if (p->mem_wb.fault != PF_NONE) {
            cpu->status = (p->mem_wb.fault == PF_MEM) ? CPU_MEM_FAULT : CPU_ILLEGAL_INSTRUCTION;
            cpu->fault_addr = p->mem_wb.fault_addr;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "fault code %d at address 0x%08x in WB", p->mem_wb.fault, p->mem_wb.fault_addr);
            return cpu->status;
        }

        if (p->mem_wb.ctrl.reg_write && p->mem_wb.instr.rd != 0) {
            uint32_t val = 0;
            if (p->mem_wb.ctrl.wb_src == WB_ALU) {
                val = p->mem_wb.alu_result;
            } else if (p->mem_wb.ctrl.wb_src == WB_MEM) {
                val = p->mem_wb.wb_val;
            } else if (p->mem_wb.ctrl.wb_src == WB_PC4) {
                val = p->mem_wb.pc + 4;
            }
            cpu_write_reg(cpu, p->mem_wb.instr.rd, val);
        }

        nx.retired++;

        if (p->mem_wb.ctrl.is_system && p->mem_wb.handler && p->mem_wb.handler->execute) {
            uint32_t old_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, &p->mem_wb.instr);
            cpu->pc = old_pc;

            if (cpu->status != CPU_RUNNING) {
                nx.if_id.valid = nx.id_ex.valid = nx.ex_mem.valid = nx.mem_wb.valid = 0;
                *p = nx;
                return cpu->status;
            }
        }
    }

    /* =========================================================================
     * 2. MEMORY (MEM)
     * ========================================================================= */
    if (p->ex_mem.valid) {
        nx.mem_wb.valid = 1;
        nx.mem_wb.pc = p->ex_mem.pc;
        nx.mem_wb.fault = p->ex_mem.fault;
        nx.mem_wb.instr = p->ex_mem.instr;
        nx.mem_wb.handler = p->ex_mem.handler;
        nx.mem_wb.ctrl = p->ex_mem.ctrl;
        nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
        nx.mem_wb.alu_result = p->ex_mem.alu_result;
        nx.mem_wb.wb_val = p->ex_mem.alu_result;

        uint32_t funct3 = p->ex_mem.instr.funct3;

        if (p->ex_mem.ctrl.mem_read) {
            uint32_t data = 0;
            int mres = MEM_OK;
            if (funct3 == 0) {
                uint8_t b;
                mres = memory_read_byte(mem, p->ex_mem.alu_result, &b);
                data = (int32_t)(int8_t)b;
            } else if (funct3 == 1) {
                uint16_t h;
                mres = memory_read_half(mem, p->ex_mem.alu_result, &h);
                data = (int32_t)(int16_t)h;
            } else if (funct3 == 2) {
                mres = memory_read_word(mem, p->ex_mem.alu_result, &data);
            } else if (funct3 == 4) {
                uint8_t b;
                mres = memory_read_byte(mem, p->ex_mem.alu_result, &b);
                data = (uint32_t)b;
            } else if (funct3 == 5) {
                uint16_t h;
                mres = memory_read_half(mem, p->ex_mem.alu_result, &h);
                data = (uint32_t)h;
            }

            if (mres != MEM_OK) {
                nx.mem_wb.fault = PF_MEM;
                nx.mem_wb.fault_addr = p->ex_mem.alu_result;
            } else {
                nx.mem_wb.wb_val = data;
            }
        } else if (p->ex_mem.ctrl.mem_write) {
            int mres = MEM_OK;
            if (funct3 == 0) {
                mres = memory_write_byte(mem, p->ex_mem.alu_result, (uint8_t)p->ex_mem.store_val);
            } else if (funct3 == 1) {
                mres = memory_write_half(mem, p->ex_mem.alu_result, (uint16_t)p->ex_mem.store_val);
            } else {
                mres = memory_write_word(mem, p->ex_mem.alu_result, p->ex_mem.store_val);
            }

            if (mres != MEM_OK) {
                nx.mem_wb.fault = PF_MEM;
                nx.mem_wb.fault_addr = p->ex_mem.alu_result;
            }
        }
    }

    /* =========================================================================
     * 3. EXECUTE (EX)
     * ========================================================================= */
    if (p->id_ex.valid) {
        nx.ex_mem.valid = 1;
        nx.ex_mem.pc = p->id_ex.pc;
        nx.ex_mem.fault = p->id_ex.fault;
        nx.ex_mem.instr = p->id_ex.instr;
        nx.ex_mem.handler = p->id_ex.handler;
        nx.ex_mem.ctrl = p->id_ex.ctrl;
        nx.ex_mem.store_val = p->id_ex.rs2_val;

        uint32_t op1 = p->id_ex.rs1_val;
        uint32_t op2 = p->id_ex.rs2_val;
        uint32_t funct3 = p->id_ex.instr.funct3;
        InstrFormat fmt = p->id_ex.handler ? p->id_ex.handler->format : FMT_R;

        if (p->id_ex.ctrl.is_branch) {
            int taken = 0;
            if (funct3 == 0) taken = (op1 == op2);
            else if (funct3 == 1) taken = (op1 != op2);
            else if (funct3 == 4) taken = ((int32_t)op1 < (int32_t)op2);
            else if (funct3 == 5) taken = ((int32_t)op1 >= (int32_t)op2);
            else if (funct3 == 6) taken = (op1 < op2);
            else if (funct3 == 7) taken = (op1 >= op2);

            if (taken) {
                uint32_t redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
                do_redirect = 1;
                nx.flushes++;
                nx.id_ex.valid = 0;
                nx.if_id.valid = 0;
                cpu->pc = redirect_pc;
            }
            nx.ex_mem.alu_result = 0;
        } else if (p->id_ex.ctrl.is_jump) {
            uint32_t redirect_pc = 0;
            if (fmt == FMT_J) {
                redirect_pc = p->id_ex.pc + p->id_ex.instr.imm;
            } else {
                redirect_pc = (op1 + p->id_ex.instr.imm) & ~1u;
            }
            do_redirect = 1;
            nx.flushes++;
            nx.id_ex.valid = 0;
            nx.if_id.valid = 0;
            cpu->pc = redirect_pc;
            nx.ex_mem.alu_result = p->id_ex.pc + 4;
        } else {
            nx.ex_mem.alu_result = alu(&p->id_ex.instr, p->id_ex.handler, p->id_ex.pc, op1, op2);
        }
    }

    /* =========================================================================
     * 4. DECODE & HAZARD DETECTION (ID)
     * ========================================================================= */
    /* Skip hazard check when EX is redirecting */
    if (p->if_id.valid && !do_redirect) {
        DecodedInstr ni;
        const InstructionHandler *nh;
        if (decode_instruction(p->if_id.raw, &ni, &nh) == DECODE_OK) {
            ControlSignals nc = control_decode(&ni, nh);

            #define HAZ(L) ((L).valid && !(L).fault && (L).ctrl.reg_write && (L).instr.rd != 0 && \
                ((nc.uses_rs1 && ni.rs1 == (L).instr.rd) || (nc.uses_rs2 && ni.rs2 == (L).instr.rd)))

            if (HAZ(p->id_ex) || HAZ(p->ex_mem)) {
                stall = 1;
                nx.stalls++;
            }
            #undef HAZ
        }
    }

    if (stall) {
        nx.if_id = p->if_id;
        nx.id_ex.valid = 0;
    } else {
        if (do_redirect) {
            nx.id_ex.valid = 0;
        } else if (p->if_id.valid) {
            DecodedInstr instr;
            const InstructionHandler *handler;
            DecodeStatus dstat = decode_instruction(p->if_id.raw, &instr, &handler);

            nx.id_ex.valid = 1;
            nx.id_ex.pc = p->if_id.pc;
            nx.id_ex.fault = p->if_id.fault;

            if (dstat != DECODE_OK) {
                nx.id_ex.fault = PF_ILLEGAL;
                nx.id_ex.ctrl = (ControlSignals){0};
                nx.id_ex.handler = NULL;
            } else {
                nx.id_ex.instr = instr;
                nx.id_ex.handler = handler;
                nx.id_ex.ctrl = control_decode(&instr, handler);
                nx.id_ex.rs1_val = cpu_read_reg(cpu, instr.rs1);
                nx.id_ex.rs2_val = cpu_read_reg(cpu, instr.rs2);
            }
        }
    }

    /* =========================================================================
     * 5. FETCH (IF)
     * ========================================================================= */
    if (!stall && cpu->status == CPU_RUNNING) {
        uint32_t fetch_pc = cpu->pc;

        uint32_t raw = 0;
        int mres = memory_read_word(mem, fetch_pc, &raw);

        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        nx.if_id.raw = raw;
        nx.if_id.fault = (mres != MEM_OK) ? PF_FETCH : PF_NONE;

        cpu->pc = fetch_pc + 4;
    }

    *p = nx;
    return cpu->status;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (p->cycles < max_cycles) {
        if (cpu->status != CPU_RUNNING && 
            !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid && !p->mem_wb.valid) {
            break;
        }
        pipeline_cycle(p, cpu, mem);
    }
}