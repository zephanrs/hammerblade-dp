include parameters.mk
include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../..)

# Hardware;

tile-x?=16
tile-y?=8
include $(HB_HAMMERBENCH_PATH)/mk/environment.mk

NUM_POD_X=$(BSG_MACHINE_PODS_X)
NUM_POD_Y=$(BSG_MACHINE_PODS_Y)
TILE_GROUP_DIM_X ?= $(tile-x)
TILE_GROUP_DIM_Y ?= $(tile-y)

vpath %.c   $(APP_PATH)
vpath %.cpp $(APP_PATH)

# Test sources;
TEST_SOURCES = main.cpp

DEFINES += -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -D_DEFAULT_SOURCE
DEFINES += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X) -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
DEFINES += -DNUM_POD_X=$(NUM_POD_X) -DNUM_POD_Y=$(NUM_POD_Y)
DEFINES += -DSIZE=$(vector-size)
ifdef repeat
DEFINES += -DREPEAT=$(repeat)
endif

FLAGS     = -g -Wall -Wno-unused-function -Wno-unused-variable
CFLAGS   += -std=c99 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)

include $(EXAMPLES_PATH)/compilation.mk

LDFLAGS +=
include $(EXAMPLES_PATH)/link.mk

# Device code;
RISCV_CCPPFLAGS += -O3 -std=c++14
RISCV_CCPPFLAGS += -I$(HB_HAMMERBENCH_PATH)/apps/common
RISCV_CCPPFLAGS += -DNUM_POD_X=$(NUM_POD_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_X=$(BSG_MACHINE_GLOBAL_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_Y=$(BSG_MACHINE_GLOBAL_Y)
RISCV_CCPPFLAGS += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X)
RISCV_CCPPFLAGS += -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
RISCV_CCPPFLAGS += -DSIZE=$(vector-size)
ifdef repeat
RISCV_CCPPFLAGS += -DREPEAT=$(repeat)
endif
# Compile-time stage bisect — see kernel.cpp.  Default 10 = full kernel.
ifdef STAGE
RISCV_CCPPFLAGS += -DSTAGE=$(STAGE)
endif

RISCV_TARGET_OBJECTS = kernel.rvo
BSG_MANYCORE_KERNELS = main.riscv
include $(EXAMPLES_PATH)/cuda/riscv.mk


# Execution args;  matches sw/1d — just the binary, no extra positional args.
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
