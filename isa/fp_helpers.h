#ifndef FP_HELPERS_H
#define FP_HELPERS_H
#include <stdint.h>
#include "cpu_state.h"
#include "decoder.h"
typedef enum { AR_ADD, AR_SUB, AR_MUL, AR_DIV, AR_SQRT, AR_FMA } ArithOp;
#define FFLAG_NX 0x01u
#define FFLAG_UF 0x02u
#define FFLAG_OF 0x04u
#define FFLAG_DZ 0x08u
#define FFLAG_NV 0x10u
#define CANONICAL_NAN 0x7fc00000u
float f_of(uint32_t bits);
uint32_t bits_of(float value);
int resolve_rm(CPUState *c, const DecodedInstr *i);
uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags);
uint32_t fp_convert_int_to_float(int64_t value, int is_unsigned, int rm, uint32_t *flags);
int64_t fp_round_to_int(double value, int rm);
#endif
