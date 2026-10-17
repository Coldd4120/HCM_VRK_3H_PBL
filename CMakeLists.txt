cmake_minimum_required(VERSION 3.20)
project(HCM_VRK_3H_PBL C ASM)
option(PBL_PRODUCTION "Require a bound production board port" OFF)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|cortex")
  message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake")
endif()

add_executable(pbl.elf
  startup/pbl_startup.S
  src/pbl_main.c
  src/pbl_core.c
  src/pbl_board_unbound.c
  src/pbl_mem.c
)
target_include_directories(pbl.elf PRIVATE include)
if(PBL_PRODUCTION)
  target_compile_definitions(pbl.elf PRIVATE PBL_PRODUCTION=1)
endif()
target_compile_features(pbl.elf PRIVATE c_std_11)
target_compile_options(pbl.elf PRIVATE
  -mcpu=cortex-m7 -mthumb -mfloat-abi=soft
  -ffreestanding -fno-builtin -fdata-sections -ffunction-sections
  -Wall -Wextra -Werror -Os
)
target_link_options(pbl.elf PRIVATE
  -mcpu=cortex-m7 -mthumb -mfloat-abi=soft
  -nostdlib -T${CMAKE_CURRENT_SOURCE_DIR}/linker/pbl_s32k338.ld
  -Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/pbl.map
)
target_link_libraries(pbl.elf PRIVATE gcc)
add_custom_command(TARGET pbl.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:pbl.elf> ${CMAKE_CURRENT_BINARY_DIR}/pbl.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:pbl.elf> ${CMAKE_CURRENT_BINARY_DIR}/pbl.bin
  VERBATIM
)
