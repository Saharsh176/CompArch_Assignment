#include "zicsr.h"
#include "isa_registry.h"
static uint32_t csr_read(CPUState*c,uint32_t n){if(n==1)return c->fcsr&31u;if(n==2)return(c->fcsr>>5)&7u;if(n==3)return c->fcsr&255u;return 0;}
static void csr_write(CPUState*c,uint32_t n,uint32_t v){if(n==1)c->fcsr=(c->fcsr&~31u)|(v&31u);else if(n==2)c->fcsr=(c->fcsr&~(7u<<5))|((v&7u)<<5);else if(n==3)c->fcsr=v&255u;}
static void exec_csr(CPUState*c,Memory*m,DecodedInstr*i){(void)m;uint32_t old=csr_read(c,i->csr),src=(i->funct3&4u)?i->rs1:cpu_read_reg(c,(int)i->rs1),v=old;int write=1;
 switch(i->funct3&3u){case 1:v=src;break;case 2:v=old|src;if(src==0)write=0;break;case 3:v=old&~src;if(src==0)write=0;break;default:return;}
 if (write) csr_write(c, i->csr, v);
 cpu_write_reg(c, (int)i->rd, old);
}
void register_zicsr(void){
 register_instruction((InstructionHandler){"csrrw",0x73,1,-1,FMT_CSR,exec_csr,0,0});
 register_instruction((InstructionHandler){"csrrs",0x73,2,-1,FMT_CSR,exec_csr,0,0});
 register_instruction((InstructionHandler){"csrrc",0x73,3,-1,FMT_CSR,exec_csr,0,0});
 register_instruction((InstructionHandler){"csrrwi",0x73,5,-1,FMT_CSR,exec_csr,0,0});
 register_instruction((InstructionHandler){"csrrsi",0x73,6,-1,FMT_CSR,exec_csr,0,0});
 register_instruction((InstructionHandler){"csrrci",0x73,7,-1,FMT_CSR,exec_csr,0,0});
}
