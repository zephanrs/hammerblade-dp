#ifndef MM_ELEM_HPP
#define MM_ELEM_HPP

// The element type is chosen per test by the dtype_ field in the test name and
// passed down as ELEM_IS_FLOAT. Both paths run the identical kernel: the RV32
// core has an FPU (RV32IMAF) for the float case and the M extension's pipelined
// multiplier for the int case.

#ifndef ELEM_IS_FLOAT
#error "ELEM_IS_FLOAT must be defined (see template.mk / test_defs.mk)"
#endif

#if ELEM_IS_FLOAT
typedef float elem_t;
#define ELEM_FMT "%f"
#define ELEM_NAME "f32"
#else
typedef int elem_t;
#define ELEM_FMT "%d"
#define ELEM_NAME "i32"
#endif

// Multiply-accumulate. -ffp-contract=fast did not get the compiler to fuse
// a*b+c: the disassembly showed separate fmul.s and fadd.s, each burning a
// full pass through the FMA datapath (fadd is rs1*1.0+rs2, fmul is
// rs1*rs2+0.0). __builtin_fmaf forces the fused form, which the hardware
// does have -- eFMADD in fpu_float_fma.sv.
//
// Safe for the result check here: FMA rounds once where mul-then-add rounds
// twice, but the operands are exactly-representable small integers, so every
// intermediate is exact either way and the comparison stays bit-exact.
#if ELEM_IS_FLOAT
static inline float elem_mac(float a, float b, float c) {
  return __builtin_fmaf(a, b, c);
}
#else
static inline int elem_mac(int a, int b, int c) {
  return c + (a * b);
}
#endif

#endif
