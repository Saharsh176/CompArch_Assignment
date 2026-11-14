#include "rv32f.h"
#include "isa_registry.h"
#include "fp_helpers.h"
#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

#define OP_LOAD_FP  0x07u
#define OP_STORE_FP 0x27u
#define OP_FP       0x53u
#define OP_MADD     0x43u
#define OP_MSUB     0x47u
#define OP_NMSUB    0x4bu
#define OP_NMADD    0x4fu
#define SIGN_BIT    0x80000000u
#define FREG(n) cpu_read_freg(c,(int)(n))
#define FFF (IF_RD_FP|IF_RS1_FP|IF_RS2_FP)

static int isnanb(uint32_t b){return (b&0x7f800000u)==0x7f800000u&&(b&0x007fffffu)!=0;}
static int isnan_s(uint32_t b){return isnanb(b)&&!(b&0x00400000u);}
static void fp_mem_fault(CPUState*c,uint32_t a,const char*op){c->status=CPU_MEM_FAULT;c->fault_addr=a;snprintf(c->fault_msg,sizeof(c->fault_msg),"%s data memory access out of bounds at address 0x%08x",op,a);}
static void exec_flw(CPUState*c,Memory*m,DecodedInstr*i){uint32_t a=cpu_read_reg(c,(int)i->rs1)+(uint32_t)i->imm,v;if(memory_read_word(m,a,&v)!=MEM_OK){fp_mem_fault(c,a,"flw");return;}cpu_write_freg(c,(int)i->rd,v);}
static void exec_fsw(CPUState*c,Memory*m,DecodedInstr*i){uint32_t a=cpu_read_reg(c,(int)i->rs1)+(uint32_t)i->imm;if(memory_write_word(m,a,FREG(i->rs2))!=MEM_OK)fp_mem_fault(c,a,"fsw");}
static void exec_fsgnj(CPUState*c,Memory*m,DecodedInstr*i){(void)m;uint32_t a=FREG(i->rs1),b=FREG(i->rs2);cpu_write_freg(c,(int)i->rd,(a&~SIGN_BIT)|(b&SIGN_BIT));}
static void exec_fsgnjn(CPUState*c,Memory*m,DecodedInstr*i){(void)m;uint32_t a=FREG(i->rs1),b=FREG(i->rs2);cpu_write_freg(c,(int)i->rd,(a&~SIGN_BIT)|(~b&SIGN_BIT));}
static void exec_fsgnjx(CPUState*c,Memory*m,DecodedInstr*i){(void)m;cpu_write_freg(c,(int)i->rd,FREG(i->rs1)^(FREG(i->rs2)&SIGN_BIT));}
static void exec_fmv_x_w(CPUState*c,Memory*m,DecodedInstr*i){(void)m;cpu_write_reg(c,(int)i->rd,FREG(i->rs1));}
static void exec_fmv_w_x(CPUState*c,Memory*m,DecodedInstr*i){(void)m;cpu_write_freg(c,(int)i->rd,cpu_read_reg(c,(int)i->rs1));}
static void exec_fclass(CPUState*c,Memory*m,DecodedInstr*i){(void)m;uint32_t b=FREG(i->rs1),e=(b>>23)&255u,f=b&0x7fffffu;int n=(b>>31)!=0,bit;
 if(e==255)bit=f?(f&0x400000u?9:8):(n?0:7);else if(e==0)bit=f?(n?2:5):(n?3:4);else bit=n?1:6;cpu_write_reg(c,(int)i->rd,1u<<bit);}
static void do_arith(CPUState*c,DecodedInstr*i,ArithOp op,uint32_t a,uint32_t b,uint32_t d){int rm=resolve_rm(c,i);if(rm<0)return;uint32_t flags=0,r=fp_arith(op,a,b,d,rm,&flags);cpu_raise_fflags(c,flags);cpu_write_freg(c,(int)i->rd,r);}
#define ARITH_FN(name,op) static void name(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_arith(c,i,op,FREG(i->rs1),FREG(i->rs2),0);}
ARITH_FN(exec_fadd,AR_ADD) ARITH_FN(exec_fsub,AR_SUB) ARITH_FN(exec_fmul,AR_MUL) ARITH_FN(exec_fdiv,AR_DIV)
static void exec_fsqrt(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_arith(c,i,AR_SQRT,FREG(i->rs1),0,0);}
static void do_fma(CPUState*c,DecodedInstr*i,int negprod,int negc){uint32_t a=FREG(i->rs1),b=FREG(i->rs2),d=FREG(i->rs3);if(negprod)a^=SIGN_BIT;if(negc)d^=SIGN_BIT;do_arith(c,i,AR_FMA,a,b,d);}
static void exec_fmadd(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_fma(c,i,0,0);}
static void exec_fmsub(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_fma(c,i,0,1);}
static void exec_fnmsub(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_fma(c,i,1,0);}
static void exec_fnmadd(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_fma(c,i,1,1);}
static void exec_feq(CPUState*c,Memory*m,DecodedInstr*i){(void)m;uint32_t a=FREG(i->rs1),b=FREG(i->rs2);if(isnan_s(a)||isnan_s(b))cpu_raise_fflags(c,FFLAG_NV);cpu_write_reg(c,(int)i->rd,!isnanb(a)&&!isnanb(b)&&f_of(a)==f_of(b));}
static void do_cmp(CPUState*c,DecodedInstr*i,int le){uint32_t a=FREG(i->rs1),b=FREG(i->rs2);if(isnanb(a)||isnanb(b)){cpu_raise_fflags(c,FFLAG_NV);cpu_write_reg(c,(int)i->rd,0);return;}cpu_write_reg(c,(int)i->rd,le?(f_of(a)<=f_of(b)):(f_of(a)<f_of(b)));}
static void exec_flt(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_cmp(c,i,0);}
static void exec_fle(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_cmp(c,i,1);}
static void do_minmax(CPUState*c,DecodedInstr*i,int max){uint32_t a=FREG(i->rs1),b=FREG(i->rs2);int na=isnanb(a),nb=isnanb(b);if(isnan_s(a)||isnan_s(b))cpu_raise_fflags(c,FFLAG_NV);uint32_t r;
 if(na&&nb)r=CANONICAL_NAN;else if(na)r=b;else if(nb)r=a;else if((a&0x7fffffffu)==0&&(b&0x7fffffffu)==0)r=max?(a&b):(a|b);else r=(max?(f_of(a)>f_of(b)):(f_of(a)<f_of(b)))?a:b;cpu_write_freg(c,(int)i->rd,r);}
static void exec_fmin(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_minmax(c,i,0);}
static void exec_fmax(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_minmax(c,i,1);}
static void exec_fcvt_w_s(CPUState*c,Memory*m,DecodedInstr*i){(void)m;int rm=resolve_rm(c,i);if(rm<0)return;uint32_t b=FREG(i->rs1);double x=(double)f_of(b);uint32_t flags=0;int32_t r;
 if(isnanb(b)){r=INT32_MAX;flags|=FFLAG_NV;}else if(x>(double)INT32_MAX){r=INT32_MAX;flags|=FFLAG_NV;}else if(x<(double)INT32_MIN){r=INT32_MIN;flags|=FFLAG_NV;}else{int64_t q=fp_round_to_int(x,rm);if(q>INT32_MAX){r=INT32_MAX;flags|=FFLAG_NV;}else if(q<INT32_MIN){r=INT32_MIN;flags|=FFLAG_NV;}else{r=(int32_t)q;if((double)q!=x)flags|=FFLAG_NX;}}cpu_raise_fflags(c,flags);cpu_write_reg(c,(int)i->rd,(uint32_t)r);}
static void exec_fcvt_wu_s(CPUState*c,Memory*m,DecodedInstr*i){(void)m;int rm=resolve_rm(c,i);if(rm<0)return;uint32_t b=FREG(i->rs1);double x=(double)f_of(b);uint32_t flags=0,r;
 if(isnanb(b)){r=UINT32_MAX;flags|=FFLAG_NV;}else if(x<0.0){r=0;flags|=FFLAG_NV;}else if(x>(double)UINT32_MAX){r=UINT32_MAX;flags|=FFLAG_NV;}else{int64_t q=fp_round_to_int(x,rm);if((long double)q>(long double)UINT32_MAX){r=UINT32_MAX;flags|=FFLAG_NV;}else{r=(uint32_t)q;if((double)q!=x)flags|=FFLAG_NX;}}cpu_raise_fflags(c,flags);cpu_write_reg(c,(int)i->rd,r);}
static void do_int_to_fp(CPUState*c,DecodedInstr*i,int uns){int rm=resolve_rm(c,i);if(rm<0)return;uint32_t flags=0;int64_t v=uns?(int64_t)(uint64_t)cpu_read_reg(c,(int)i->rs1):(int64_t)(int32_t)cpu_read_reg(c,(int)i->rs1);uint32_t r=fp_convert_int_to_float(v,uns,rm,&flags);cpu_raise_fflags(c,flags);cpu_write_freg(c,(int)i->rd,r);}
static void exec_fcvt_s_w(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_int_to_fp(c,i,0);}
static void exec_fcvt_s_wu(CPUState*c,Memory*m,DecodedInstr*i){(void)m;do_int_to_fp(c,i,1);}

static void reg(const char*n,uint32_t op,int f3,int f7,InstrFormat fmt,ExecuteFn fn,uint32_t flags,uint32_t rs2){register_instruction_ex((InstructionHandler){n,op,f3,f7,fmt,fn,0,0},flags,rs2);}
void register_rv32f(void){
 reg("flw",OP_LOAD_FP,2,-1,FMT_I_LOAD,exec_flw,IF_RD_FP,0);
 reg("fsw",OP_STORE_FP,2,-1,FMT_S,exec_fsw,IF_RS2_FP,0);
 reg("fsgnj.s",OP_FP,0,0x10,FMT_R,exec_fsgnj,FFF,0);reg("fsgnjn.s",OP_FP,1,0x10,FMT_R,exec_fsgnjn,FFF,0);reg("fsgnjx.s",OP_FP,2,0x10,FMT_R,exec_fsgnjx,FFF,0);
 reg("fmv.x.w",OP_FP,0,0x70,FMT_R2,exec_fmv_x_w,IF_RS1_FP|IF_RS2_FIXED,0);
 reg("fmv.w.x",OP_FP,0,0x78,FMT_R2,exec_fmv_w_x,IF_RD_FP|IF_RS2_FIXED,0);
 reg("fclass.s",OP_FP,1,0x70,FMT_R2,exec_fclass,IF_RS1_FP|IF_RS2_FIXED,0);
 reg("fadd.s",OP_FP,-1,0x00,FMT_R,exec_fadd,FFF|IF_HAS_RM,0);reg("fsub.s",OP_FP,-1,0x04,FMT_R,exec_fsub,FFF|IF_HAS_RM,0);reg("fmul.s",OP_FP,-1,0x08,FMT_R,exec_fmul,FFF|IF_HAS_RM,0);reg("fdiv.s",OP_FP,-1,0x0c,FMT_R,exec_fdiv,FFF|IF_HAS_RM,0);
 reg("fsqrt.s",OP_FP,-1,0x2c,FMT_R2,exec_fsqrt,IF_RD_FP|IF_RS1_FP|IF_RS2_FIXED|IF_HAS_RM,0);
 reg("fmadd.s",OP_MADD,-1,-1,FMT_R4,exec_fmadd,FFF|IF_RS3_FP|IF_HAS_RM,0);reg("fmsub.s",OP_MSUB,-1,-1,FMT_R4,exec_fmsub,FFF|IF_RS3_FP|IF_HAS_RM,0);reg("fnmsub.s",OP_NMSUB,-1,-1,FMT_R4,exec_fnmsub,FFF|IF_RS3_FP|IF_HAS_RM,0);reg("fnmadd.s",OP_NMADD,-1,-1,FMT_R4,exec_fnmadd,FFF|IF_RS3_FP|IF_HAS_RM,0);
 reg("feq.s",OP_FP,2,0x50,FMT_R,exec_feq,IF_RS1_FP|IF_RS2_FP,0);reg("flt.s",OP_FP,1,0x50,FMT_R,exec_flt,IF_RS1_FP|IF_RS2_FP,0);reg("fle.s",OP_FP,0,0x50,FMT_R,exec_fle,IF_RS1_FP|IF_RS2_FP,0);
 reg("fmin.s",OP_FP,0,0x14,FMT_R,exec_fmin,FFF,0);reg("fmax.s",OP_FP,1,0x14,FMT_R,exec_fmax,FFF,0);
 reg("fcvt.w.s",OP_FP,-1,0x60,FMT_R2,exec_fcvt_w_s,IF_RS1_FP|IF_RS2_FIXED|IF_HAS_RM,0);reg("fcvt.wu.s",OP_FP,-1,0x60,FMT_R2,exec_fcvt_wu_s,IF_RS1_FP|IF_RS2_FIXED|IF_HAS_RM,1);
 reg("fcvt.s.w",OP_FP,-1,0x68,FMT_R2,exec_fcvt_s_w,IF_RD_FP|IF_RS2_FIXED|IF_HAS_RM,0);reg("fcvt.s.wu",OP_FP,-1,0x68,FMT_R2,exec_fcvt_s_wu,IF_RD_FP|IF_RS2_FIXED|IF_HAS_RM,1);
}
