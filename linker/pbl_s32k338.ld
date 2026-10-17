ENTRY(Reset_Handler)
MEMORY
{
  PBL_FLASH (rx) : ORIGIN = 0x00400000, LENGTH = 0x00040000
  PBL_SRAM (rwx) : ORIGIN = 0x20400000, LENGTH = 0x00010000
  PBL_DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00010000
}
__stack_top = ORIGIN(PBL_DTCM) + LENGTH(PBL_DTCM);
SECTIONS
{
  .boot_header ORIGIN(PBL_FLASH) :
  {
    KEEP(*(.boot_header))
  } > PBL_FLASH
  .isr_vector ORIGIN(PBL_FLASH) + 0x2000 :
  {
    KEEP(*(.isr_vector))
  } > PBL_FLASH
  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > PBL_FLASH
  .data :
  {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > PBL_SRAM AT > PBL_FLASH
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) :
  {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > PBL_SRAM
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.boot_header) <= 0x2000, "PBL IVT overlaps vector")
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(PBL_FLASH) + LENGTH(PBL_FLASH),
       "PBL exceeds reserved 256 KiB")
