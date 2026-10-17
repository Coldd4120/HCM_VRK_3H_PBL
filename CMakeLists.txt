cmake_minimum_required(VERSION 3.20)
project(HCM_VRK_3H_PBL C ASM)
option(PBL_PRODUCTION "Require a bound production board port" OFF)
set(PBL_BOARD "unbound" CACHE STRING "Board binding: unbound (fail-closed) or s32k338")
set_property(CACHE PBL_BOARD PROPERTY STRINGS unbound s32k338)
# LH-R1-002/003: opt-in TxnRecordV1 boot path. OFF keeps the release image
# byte-identical (neither the define nor the txn sources are added).
option(PBL_TXN_BACKEND "Use the TxnRecordV1 boot-decision path (LH-R1-002/003)" OFF)
# Bench bring-up: direct primary-C0 boot, no HSE/journal. NOT for production.
option(PBL_BENCH_BRINGUP "Bench bring-up: direct primary-C0 boot, no HSE/journal (NOT for production)" OFF)
# NXP RTD device headers (S32K358_*.h, used for S32K338). Referenced in place,
# never copied into this repository (NXP proprietary).
set(PBL_RTD_HEADER_DIR
  "D:/NXP/S32DS.3.6.7/S32DS/software/PlatformSDK_S32K3/RTD/BaseNXP_TS_T40D34M60I0R0/header"
  CACHE PATH "RTD BaseNXP header directory")

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|cortex")
  message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake")
endif()

set(PBL_SOURCES
  startup/pbl_startup.S
  src/pbl_main.c
  src/pbl_core.c
  src/pbl_uds.c
  src/pbl_flash_policy.c
  src/pbl_mem.c
)
if(PBL_BOARD STREQUAL "unbound")
  list(APPEND PBL_SOURCES src/pbl_board_unbound.c)
elseif(PBL_BOARD STREQUAL "s32k338")
  if(NOT EXISTS "${PBL_RTD_HEADER_DIR}/S32K358_SIUL2.h")
    message(FATAL_ERROR "PBL_BOARD=s32k338 needs PBL_RTD_HEADER_DIR with the RTD S32K358 headers")
  endif()
  list(APPEND PBL_SOURCES
    src/hal/vrk_hse_channel.c
    src/board/s32k338/pbl_board_s32k338.c
    src/board/s32k338/pbl_hw_s32k338.c
    src/board/s32k338/pbl_c40.c)
else()
  message(FATAL_ERROR "Unknown PBL_BOARD '${PBL_BOARD}'")
endif()

if(PBL_TXN_BACKEND)
  list(APPEND PBL_SOURCES
    src/txn/txn_record.c
    src/txn/txn_store.c
    src/txn/txn_pbl.c)
endif()

add_executable(pbl.elf ${PBL_SOURCES})
target_include_directories(pbl.elf PRIVATE include)
if(PBL_BOARD STREQUAL "s32k338")
  target_include_directories(pbl.elf PRIVATE src/board/s32k338)
  target_include_directories(pbl.elf SYSTEM PRIVATE "${PBL_RTD_HEADER_DIR}")
endif()
if(PBL_PRODUCTION)
  target_compile_definitions(pbl.elf PRIVATE PBL_PRODUCTION=1)
endif()
if(PBL_TXN_BACKEND)
  target_compile_definitions(pbl.elf PRIVATE PBL_TXN_BACKEND=1)
endif()
if(PBL_BENCH_BRINGUP)
  target_compile_definitions(pbl.elf PRIVATE PBL_BENCH_BRINGUP=1)
endif()
target_compile_features(pbl.elf PRIVATE c_std_11)
target_compile_options(pbl.elf PRIVATE
  -mcpu=cortex-m7 -mthumb -mfloat-abi=soft
  -ffreestanding -fno-builtin -fdata-sections -ffunction-sections
  -Wall -Wextra -Werror -Os
)
# Generated layout (linker/gen/pbl_layout.ld) is INCLUDEd relative to -L.
target_link_options(pbl.elf PRIVATE
  -mcpu=cortex-m7 -mthumb -mfloat-abi=soft
  -nostdlib -L${CMAKE_CURRENT_SOURCE_DIR}/linker
  -T${CMAKE_CURRENT_SOURCE_DIR}/linker/pbl_s32k338.ld
  -Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/pbl.map
)
set_property(TARGET pbl.elf APPEND PROPERTY LINK_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/linker/pbl_s32k338.ld
  ${CMAKE_CURRENT_SOURCE_DIR}/linker/gen/pbl_layout.ld)
target_link_libraries(pbl.elf PRIVATE gcc)
add_custom_command(TARGET pbl.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:pbl.elf> ${CMAKE_CURRENT_BINARY_DIR}/pbl.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:pbl.elf> ${CMAKE_CURRENT_BINARY_DIR}/pbl.bin
  VERBATIM
)
