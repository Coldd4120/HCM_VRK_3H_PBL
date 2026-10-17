/* PBL linker script (Q08). Memory regions and layout symbols come from the
 * generated linker/gen/pbl_layout.ld (common/boot/layout.json via
 * tools/layout/gen_layout.py in HCM_VRK_3H); no literal ORIGIN here (R9).
 * RAM: .data/.bss/.ramfunc in PBL_SRAM (0x20420000..0x2042FFFF, the C0
 * non-cacheable SRAM that C0 re-initialises on every reset and that is
 * outside __STANDBY_RAM_LIMIT_END 0x2040FFFF), stack at the top of
 * PBL_DTCM. The C0 standby-retained SRAM 0x20400000..0x2040FFFF is never
 * touched (layout rule R11). */
INCLUDE gen/pbl_layout.ld

ENTRY(Reset_Handler)
__stack_top = ORIGIN(PBL_DTCM) + LENGTH(PBL_DTCM);
__pbl_dtcm_start = ORIGIN(PBL_DTCM);
__pbl_dtcm_end = ORIGIN(PBL_DTCM) + LENGTH(PBL_DTCM);
__pbl_sram_start = ORIGIN(PBL_SRAM);
__pbl_sram_end = ORIGIN(PBL_SRAM) + LENGTH(PBL_SRAM);
__pbl_vector_start = ORIGIN(PBL_FLASH) + PBL_LAYOUT_VECTOR_OFFSET;

SECTIONS
{
  .boot_header ORIGIN(PBL_FLASH) :
  {
    KEEP(*(.boot_header))
  } > PBL_FLASH
  .isr_vector __pbl_vector_start :
  {
    KEEP(*(.isr_vector))
  } > PBL_FLASH
  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(8);
  } > PBL_FLASH
  .data :
  {
    __data_start = .;
    *(.ramfunc .ramfunc.*)
    *(.data .data.*)
    . = ALIGN(8);
    __data_end = .;
  } > PBL_SRAM AT > PBL_FLASH
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) :
  {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > PBL_SRAM
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) }
}
ASSERT(SIZEOF(.boot_header) <= PBL_LAYOUT_VECTOR_OFFSET, "PBL IVT overlaps vector")
ASSERT(ADDR(.isr_vector) == __pbl_vector_start, "PBL vector not at layout vector_offset")
ASSERT(SIZEOF(.isr_vector) == (16 + 249) * 4, "PBL vector table must be complete")
ASSERT((__pbl_vector_start % 0x800) == 0, "PBL VTOR alignment: 265 vectors need 0x800")
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(PBL_FLASH) + LENGTH(PBL_FLASH),
       "PBL exceeds its layout region")
