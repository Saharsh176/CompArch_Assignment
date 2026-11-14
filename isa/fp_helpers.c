#include "fp_helpers.h"
#include <fenv.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <float.h>

float f_of(uint32_t b) { float f; memcpy(&f,&b,sizeof f); return f; }
uint32_t bits_of(float f) { uint32_t b; memcpy(&b,&f,sizeof b); return b; }
static int is_nan_bits(uint32_t b) { return (b & 0x7f800000u)==0x7f800000u && (b & 0x007fffffu)!=0; }
static int is_snan_bits(uint32_t b) { return is_nan_bits(b) && !(b & 0x00400000u); }
static int host_mode(int rm) { switch(rm){case 0:return FE_TONEAREST;case 1:return FE_TOWARDZERO;case 2:return FE_DOWNWARD;case 3:return FE_UPWARD;default:return FE_TONEAREST;} }
static uint32_t fflags_of(int e) {
    uint32_t f = 0;
    if (e & FE_INVALID) f |= FFLAG_NV;
    if (e & FE_DIVBYZERO) f |= FFLAG_DZ;
    if (e & FE_OVERFLOW) f |= FFLAG_OF;
    if (e & FE_UNDERFLOW) f |= FFLAG_UF;
    if (e & FE_INEXACT) f |= FFLAG_NX;
    return f;
}
int resolve_rm(CPUState *c, const DecodedInstr *i) {
    int rm=(int)i->funct3; if(rm==7) rm=cpu_frm(c);
    if(rm<0 || rm>4){ c->status=CPU_ILLEGAL_INSTRUCTION; c->fault_addr=c->pc;
        strcpy(c->fault_msg,"illegal floating-point rounding mode"); return -1; }
    return rm;
}
static float round_double_to_float(double x, int rm) {
    int old=fegetround(); fesetround(rm==4?FE_TONEAREST:host_mode(rm));
    volatile double vx=x; volatile float y=(float)vx; float out=y;
    if(rm==4 && isfinite(x) && isfinite(out)) {
        float lo=out, hi=out;
        if((double)out < x) hi=nextafterf(out, INFINITY); else if((double)out > x) lo=nextafterf(out,-INFINITY);
        if((double)lo < x && (double)hi > x && x- (double)lo == (double)hi-x)
            out = (fabs((double)lo) > fabs((double)hi)) ? lo : hi;
    }
    fesetround(old); return out;
}
int64_t fp_round_to_int(double x, int rm) {
    double fl=floor(x), frac=x-fl;
    switch(rm){case 1:return (int64_t)trunc(x);case 2:return (int64_t)fl;case 3:return (int64_t)ceil(x);
        case 4: if(frac<0.5)return (int64_t)fl; if(frac>0.5)return (int64_t)(fl+1.0); return x<0?(int64_t)fl:(int64_t)(fl+1.0);
        default: if(frac<0.5)return (int64_t)fl; if(frac>0.5)return (int64_t)(fl+1.0); { int64_t q=(int64_t)fl; return (q&1)?q+1:q; }}
}
uint32_t fp_convert_int_to_float(int64_t value, int is_unsigned, int rm, uint32_t *flags) {
    double d=is_unsigned?(double)(uint64_t)value:(double)value;
    feclearexcept(FE_ALL_EXCEPT); float f=round_double_to_float(d,rm);
    if((double)f != d) *flags |= FFLAG_NX;
    if(isinf(f) && isfinite(d)) *flags |= FFLAG_OF|FFLAG_NX;
    return bits_of(f);
}
uint32_t fp_arith(ArithOp op,uint32_t A,uint32_t B,uint32_t C,int rm,uint32_t *flags) {
    float a=f_of(A),b=f_of(B),c=f_of(C),r=0.0f;
    int old=fegetround(); feclearexcept(FE_ALL_EXCEPT); fesetround(rm==4?FE_TONEAREST:host_mode(rm));
    volatile float va=a,vb=b,vc=c;
    switch(op){case AR_ADD:r=va+vb;break;case AR_SUB:r=va-vb;break;case AR_MUL:r=va*vb;break;case AR_DIV:r=va/vb;break;case AR_SQRT:r=sqrtf(va);break;case AR_FMA:r=fmaf(va,vb,vc);break;}
    int ex=fetestexcept(FE_ALL_EXCEPT); *flags |= fflags_of(ex);
    if(is_snan_bits(A)||is_snan_bits(B)||((op==AR_FMA)&&is_snan_bits(C))) *flags |= FFLAG_NV;
    if(op==AR_FMA && ((isinf(a)&&b==0.0f)||(isinf(b)&&a==0.0f))) *flags |= FFLAG_NV;
    if (rm == 4 && isfinite(a) && isfinite(b) &&
        (op == AR_ADD || op == AR_SUB || op == AR_MUL || op == AR_FMA ||
         (op == AR_DIV && b != 0.0f) || (op == AR_SQRT && a >= 0.0f))) {
        double exact = 0.0;
        if (op == AR_ADD) exact = (double)a + (double)b;
        else if (op == AR_SUB) exact = (double)a - (double)b;
        else if (op == AR_MUL) exact = (double)a * (double)b;
        else if (op == AR_FMA) exact = fma((double)a, (double)b, (double)c);
        else if (op == AR_DIV) exact = (double)a / (double)b;
        else exact = sqrt((double)a);
        float rr = round_double_to_float(exact, 4);
        if ((double)rr != exact) *flags |= FFLAG_NX;
        if (isinf(rr) && isfinite(exact)) *flags |= FFLAG_OF | FFLAG_NX;
        if (fabsf(rr) < FLT_MIN && (double)rr != exact) *flags |= FFLAG_UF | FFLAG_NX;
        r = rr;
    }
    fesetround(old);
    if(isnan(r)) return CANONICAL_NAN;
    return bits_of(r);
}
