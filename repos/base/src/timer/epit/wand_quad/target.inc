TARGET   = wand_quad_timer_drv
REQUIRES = wand_quad
GEN_DIR := $(dir $(call select_from_repositories,src/timer/main.cc))
INC_DIR += $(GEN_DIR)/epit
SRC_CC  += epit/time_source.cc epit/wand_quad/timer.cc

include $(GEN_DIR)/target.inc

vpath %.cc $(GEN_DIR)
