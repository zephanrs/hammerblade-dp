include parameters.mk
include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../..)
# BSG_MACHINE_PATH comes from the cluster environment; do not override here.
include $(HB_HAMMERBENCH_PATH)/mk/environment.mk


###############################################################################
# Host code compilation flags and flow
###############################################################################
# import parameters and APP_PATH

# Tile Group Dimensions
TILE_GROUP_DIM_X ?= 16
TILE_GROUP_DIM_Y ?= 8

vpath %.c   $(APP_PATH)
vpath %.cpp $(APP_PATH)

# TEST_SOURCES is a list of source files that need to be compiled
TEST_SOURCES = main.c

DEFINES += -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -D_DEFAULT_SOURCE
DEFINES += -DSIZE=$(vector-size)
CDEFINES += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X) -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
CDEFINES += -DCACHE_LINE_WORDS=$(BSG_MACHINE_VCACHE_LINE_WORDS)
CXXDEFINES +=

FLAGS     = -g -Wall -Wno-unused-function -Wno-unused-variable
CFLAGS   += -std=c99 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)
# compilation.mk defines rules for compilation of C/C++
include $(EXAMPLES_PATH)/compilation.mk

###############################################################################
# Host code link flags and flow
###############################################################################

LDFLAGS +=

# link.mk defines rules for linking of the final execution binary.
include $(EXAMPLES_PATH)/link.mk

###############################################################################
# Device code compilation flow
###############################################################################

# BSG_MANYCORE_KERNELS is a list of manycore executables that should
# be built before executing.

RISCV_CCPPFLAGS += -O3 -std=c++14
RISCV_CCPPFLAGS += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X)
RISCV_CCPPFLAGS += -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
RISCV_CCPPFLAGS += -DCACHE_LINE_WORDS=$(BSG_MACHINE_VCACHE_LINE_WORDS)
ifeq ($(warm-cache),yes)
RISCV_CCPPFLAGS += -DWARM_CACHE
endif
# NOTE: -flto removed. With LTO the linker may strip the `kernel` symbol
# from the binary because no in-binary call site references it (the host
# runtime calls it by name string). That causes the cores to never enter
# user code, the first barrier_sync hangs, and the host hangs waiting
# for kernel completion. sw/1d doesn't use -flto either.
# RISCV_LDFLAGS += -flto
RISCV_TARGET_OBJECTS = kernel.rvo
# Set USE_LINEAR_BARRIER=1 to override bsg_barrier_amoadd with the AMOADD
# linear barrier from barriers/linear_barrier.S. Linking it replaces the
# library's default bsg_barrier_amoadd symbol; no kernel code changes needed.
BARRIERS_PATH := $(abspath $(APP_PATH)/../../barriers)
vpath %.S $(BARRIERS_PATH)
ifdef USE_LINEAR_BARRIER
RISCV_TARGET_OBJECTS += linear_barrier.rvo
endif
BSG_MANYCORE_KERNELS = main.riscv

include $(EXAMPLES_PATH)/cuda/riscv.mk
###############################################################################
# Execution flow
#
# C_ARGS: Use this to pass arguments that you want to appear in argv
#         For SPMD tests C arguments are: <Path to RISC-V Binary> <Test Name>
#
# SIM_ARGS: Use this to pass arguments to the simulator
###############################################################################
C_ARGS ?= $(BSG_MANYCORE_KERNELS) merge_sort

SIM_ARGS ?=

# Include platform-specific execution rules
include $(EXAMPLES_PATH)/execution.mk

###############################################################################
# Regression Flow
###############################################################################

regression: exec.log
	@grep "BSG REGRESSION TEST .*PASSED.*" $< > /dev/null

.DEFAULT_GOAL := help


wave:
	verdi -ssf debug.fsdb &
