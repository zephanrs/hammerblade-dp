include parameters.mk
include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)

# Element type; dtype comes from the test name via parameters.mk.
ifeq ($(dtype),f32)
ELEM_DEFINES := -DELEM_IS_FLOAT=1
else ifeq ($(dtype),i32)
ELEM_DEFINES := -DELEM_IS_FLOAT=0
else
$(error unknown dtype '$(dtype)': expected f32 or i32)
endif

# Hardware;

# tile-x / tile-y come from parameters.mk (the tgx_/tgy_ fields of the test
# name). These ?= defaults only apply if a test omits them. A smaller tile
# group is how the multi-tile decomposition gets validated under RTL.
tile-x?=16
tile-y?=8
# Real-hardware runs pick BSG_MACHINE_PATH from the cluster environment.
# Do not assign it here: an override would defeat the command line and hang on
# hardware. For RTL simulation pass a machine on the command line, e.g.
#   make profile.log \
#     BSG_MACHINE_PATH=$(REPLICANT_PATH)/machines/bigblade_pod_X1Y1_ruche_X16Y8_hbm_one_pseudo_channel
include $(HB_HAMMERBENCH_PATH)/mk/environment.mk

# number of pods participating in barrier;
NUM_POD_X=$(BSG_MACHINE_PODS_X)
NUM_POD_Y=$(BSG_MACHINE_PODS_Y)
# Tile group DIM
TILE_GROUP_DIM_X ?= $(tile-x)
TILE_GROUP_DIM_Y ?= $(tile-y)

vpath %.c   $(APP_PATH)
vpath %.cpp $(APP_PATH)

# Test sources;
TEST_SOURCES = main.cpp

DEFINES += -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -D_DEFAULT_SOURCE
DEFINES += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X) -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
DEFINES += -DNUM_POD_X=$(NUM_POD_X) -DNUM_POD_Y=$(NUM_POD_Y)
DEFINES += -DMAT_M=$(mat-m) -DMAT_N=$(mat-n) -DMAT_K=$(mat-k) -DBLK_K=$(blk-k)
DEFINES += $(ELEM_DEFINES)

FLAGS     = -g -Wall -Wno-unused-function -Wno-unused-variable
CFLAGS   += -std=c99 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)


# compilation rules;
include $(EXAMPLES_PATH)/compilation.mk

# Linker rules;
LDFLAGS +=
include $(EXAMPLES_PATH)/link.mk


# Device code;
RISCV_CCPPFLAGS += -O3 -std=c++14
# fadd and fmul each burn a full pass through the FMA datapath (fadd is
# rs1*1.0+rs2, fmul is rs1*rs2+0.0), so contracting a*b+c into one fmadd.s
# halves the FP issue slots in the inner loop at identical latency.
RISCV_CCPPFLAGS += -ffp-contract=fast
RISCV_CCPPFLAGS += -I$(HB_HAMMERBENCH_PATH)/apps/common
RISCV_CCPPFLAGS += -DNUM_POD_X=$(NUM_POD_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_X=$(BSG_MACHINE_GLOBAL_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_Y=$(BSG_MACHINE_GLOBAL_Y)
RISCV_CCPPFLAGS += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X)
RISCV_CCPPFLAGS += -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
RISCV_CCPPFLAGS += -DMAT_M=$(mat-m) -DMAT_N=$(mat-n) -DMAT_K=$(mat-k) -DBLK_K=$(blk-k)
RISCV_CCPPFLAGS += $(ELEM_DEFINES)

RISCV_TARGET_OBJECTS = kernel.rvo
BSG_MANYCORE_KERNELS = main.riscv
include $(EXAMPLES_PATH)/cuda/riscv.mk


# Execution args;
C_ARGS ?= $(BSG_MANYCORE_KERNELS)

SIM_ARGS ?=  +vcs+nostdout


# Exec rules;
include $(EXAMPLES_PATH)/execution.mk
RUN_RULES += saifgen.log
RUN_RULES += repl.log
RUN_RULES += pc-histogram.log
RUN_RULES += debug.log
RUN_RULES += profile.log
RUN_RULES += exec.log
.DEFAULT_GOAL := help
