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

#endif
