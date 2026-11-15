# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "E:\\02_Proj\\00_FPGA\\SDK\\WorkSpace\\linux2\\platform\\ps7_cortexa9_0\\standalone_ps7_cortexa9_0\\bsp\\include\\sleep.h"
  "E:\\02_Proj\\00_FPGA\\SDK\\WorkSpace\\linux2\\platform\\ps7_cortexa9_0\\standalone_ps7_cortexa9_0\\bsp\\include\\xiltimer.h"
  "E:\\02_Proj\\00_FPGA\\SDK\\WorkSpace\\linux2\\platform\\ps7_cortexa9_0\\standalone_ps7_cortexa9_0\\bsp\\include\\xtimer_config.h"
  "E:\\02_Proj\\00_FPGA\\SDK\\WorkSpace\\linux2\\platform\\ps7_cortexa9_0\\standalone_ps7_cortexa9_0\\bsp\\lib\\libxiltimer.a"
  )
endif()
