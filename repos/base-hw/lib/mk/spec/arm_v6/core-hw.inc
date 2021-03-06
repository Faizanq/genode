#
# \brief  Build config for Genodes core process
# \author Martin Stein
# \author Stefan Kalkowski
# \date   2012-10-04
#

# add include paths
INC_DIR += $(BASE_DIR)/../base-hw/src/core/spec/arm_v6

# add C++ sources
SRC_CC += spec/arm_v6/cpu.cc
SRC_CC += spec/arm_v6/perf_counter.cc
SRC_CC += kernel/vm_thread_off.cc
SRC_CC += kernel/cpu_up.cc

SRC_S  += spec/arm/vfpv2.s

# include less specific configuration
include $(BASE_DIR)/../base-hw/lib/mk/spec/arm/core-hw.inc
