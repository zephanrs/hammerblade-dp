include parameters.mk
include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)

# Hardware;

tile-x?=16
tile-y?=8
include $(HB_HAMMERBENCH_PATH)/mk/environment.mk

TILE_GROUP_DIM_X ?= $(tile-x)
TILE_GROUP_DIM_Y ?= $(tile-y)

# Source dirs.
vpath %.c   $(APP_PATH)
vpath %.cpp $(APP_PATH)
# barriers/ lives at the apps/programs root (one level above dummy/).
vpath %.S   $(APP_PATH)/../../barriers

# Test sources;
TEST_SOURCES = main.cpp

DEFINES += -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -D_DEFAULT_SOURCE
DEFINES += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X) -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
DEFINES += -DN_BARRIERS=$(n)
DEFINES += -DBARRIER_LABEL=\"$(barrier)\"

FLAGS     = -g -Wall -Wno-unused-function -Wno-unused-variable
CFLAGS   += -std=c99 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)

include $(EXAMPLES_PATH)/compilation.mk

LDFLAGS +=
include $(EXAMPLES_PATH)/link.mk

# Device code;
RISCV_CCPPFLAGS += -O3 -std=c++14
RISCV_CCPPFLAGS += -I$(HB_HAMMERBENCH_PATH)/apps/common
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_X=$(BSG_MACHINE_GLOBAL_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_Y=$(BSG_MACHINE_GLOBAL_Y)
RISCV_CCPPFLAGS += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X)
RISCV_CCPPFLAGS += -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
RISCV_CCPPFLAGS += -DN_BARRIERS=$(n)

# Pick the barrier implementation by linking the chosen .S earlier than
# the BSG manycore archive.  Both files define the same symbol
# (bsg_barrier_amoadd); the linker takes the user object and skips the
# archive copy, so bsg_barrier_tile_group_sync's inline call resolves
# to whichever implementation we supplied.
ifeq ($(barrier),linear)
RISCV_TARGET_OBJECTS = kernel.rvo linear_barrier.rvo
else
RISCV_TARGET_OBJECTS = kernel.rvo
endif

BSG_MANYCORE_KERNELS = main.riscv
include $(EXAMPLES_PATH)/cuda/riscv.mk

# .S → .rvo build rule (riscv.mk only ships %.rvo:%.c and %.rvo:%.cpp).
%.rvo: %.S
	$(_RISCV_GCC) $(RISCV_CFLAGS) $(RISCV_DEFINES) -D__ASSEMBLY__=1 $(RISCV_INCLUDES) -c $< -o $@ 2>&1 | tee $*.rvo.log

C_ARGS ?= $(BSG_MANYCORE_KERNELS)
SIM_ARGS ?= +vcs+nostdout

include $(EXAMPLES_PATH)/execution.mk
RUN_RULES += saifgen.log
RUN_RULES += repl.log
RUN_RULES += pc-histogram.log
RUN_RULES += debug.log
RUN_RULES += profile.log
RUN_RULES += exec.log
.DEFAULT_GOAL := help
