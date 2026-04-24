include parameters.mk
include app_path.mk
HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)

tile-x?=16
tile-y?=8
include $(HB_HAMMERBENCH_PATH)/mk/environment.mk

TILE_GROUP_DIM_X ?= $(tile-x)
TILE_GROUP_DIM_Y ?= $(tile-y)

vpath %.c   $(APP_PATH)
vpath %.cpp $(APP_PATH)

TEST_SOURCES = main.cpp

DEFINES += -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -D_DEFAULT_SOURCE
DEFINES += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X) -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
DEFINES += -DNUM_POD_X=$(BSG_MACHINE_PODS_X)
ifdef ops
DEFINES += -DOPS_PER_ELEM=$(ops)
endif
ifdef n-elems
DEFINES += -DN_ELEMS=$(n-elems)
endif
ifdef repeat
DEFINES += -DREPEAT=$(repeat)
endif

FLAGS     = -g -Wall -Wno-unused-function -Wno-unused-variable
CFLAGS   += -std=c99 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)

include $(EXAMPLES_PATH)/compilation.mk

LDFLAGS +=
include $(EXAMPLES_PATH)/link.mk

RISCV_CCPPFLAGS += -O3 -std=c++14
RISCV_CCPPFLAGS += -I$(HB_HAMMERBENCH_PATH)/apps/common
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_X=$(BSG_MACHINE_GLOBAL_X)
RISCV_CCPPFLAGS += -DBSG_MACHINE_GLOBAL_Y=$(BSG_MACHINE_GLOBAL_Y)
RISCV_CCPPFLAGS += -Dbsg_tiles_X=$(TILE_GROUP_DIM_X)
RISCV_CCPPFLAGS += -Dbsg_tiles_Y=$(TILE_GROUP_DIM_Y)
ifdef ops
RISCV_CCPPFLAGS += -DOPS_PER_ELEM=$(ops)
endif
ifdef n-elems
RISCV_CCPPFLAGS += -DN_ELEMS=$(n-elems)
endif
ifdef repeat
RISCV_CCPPFLAGS += -DREPEAT=$(repeat)
endif

RISCV_TARGET_OBJECTS = kernel.rvo
BSG_MANYCORE_KERNELS = main.riscv
include $(EXAMPLES_PATH)/cuda/riscv.mk

C_ARGS ?= $(BSG_MANYCORE_KERNELS)
SIM_ARGS ?= +vcs+nostdout

include $(EXAMPLES_PATH)/execution.mk
RUN_RULES += exec.log
.DEFAULT_GOAL := help
