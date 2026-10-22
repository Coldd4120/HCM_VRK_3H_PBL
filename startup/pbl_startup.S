.syntax unified
.cpu cortex-m7
.thumb

/* S32K338 PBL start-up (Q08).
 *
 * SBAF IVT at the PBL region start (layout regions[pbl], vector_offset 0x2000):
 * word layout as the main project's startup_cm7.s .boot_header. Only CM7_0 is
 * enabled; it starts at the PBL vector table (__pbl_vector_start, checked by
 * the linker against the generated PBL_LAYOUT_VECTOR_OFFSET). HSE 2.55/SBAF
 * installation and IVT protection must be validated on the target board. */
.section .boot_header, "a", %progbits
.word 0x5AA55AA5            /* 0x00 IVT marker (SBAF_BOOT_MARKER) */
.word 0x00000001            /* 0x04 boot configuration: CM7_0 enable */
.word 0xFFFFFFFF            /* 0x08 reserved */
.word __pbl_vector_start    /* 0x0C CM7_0 start address */
.word 0xFFFFFFFF            /* 0x10 reserved */
.word 0xFFFFFFFF            /* 0x14 CM7_1 start address (unused) */
.word 0xFFFFFFFF            /* 0x18 reserved */
.word 0xFFFFFFFF            /* 0x1C CM7_2 start address (unused) */
.word 0xFFFFFFFF            /* 0x20 reserved */
.word 0x00000000            /* 0x24 lifecycle configuration pointer (LC_CONFIG_ADDR 0) */
.word 0xFFFFFFFF            /* 0x28 reserved */
#if defined(PBL_HSE_INSTALL_SOURCE)
.word PBL_HSE_INSTALL_SOURCE /* 0x2C factory candidate only, own build identity */
#else
.word 0xFFFFFFFF            /* 0x2C runtime: no HSE installation source */
#endif
.rept 52
.word 0xFFFFFFFF            /* 0x30..0xFC reserved / CM7_3 unused */
.endr

/* Full Cortex-M7 vector table: 16 system entries + 242 peripheral entries
 * (IRQn 0..241) = 258 words, the same length the C0 image links
 * (Vector_Table.s 0x408 bytes), so an interrupt that C0 enables can never
 * run past the end of the PBL table. NUMBER_OF_INT_VECTORS (249) in the RTD
 * S32K358_COMMON.h used for S32K338 is smaller; the image is the authority.
 * Every entry is valid, so a stray exception or interrupt lands in
 * Default_Handler, never in erased flash. */
.section .isr_vector, "a", %progbits
.global __pbl_vectors
__pbl_vectors:
.word __stack_top
.word Reset_Handler + 1
.word Default_Handler + 1   /* NMI */
.word Default_Handler + 1   /* HardFault */
.word Default_Handler + 1   /* MemManage */
.word Default_Handler + 1   /* BusFault */
.word Default_Handler + 1   /* UsageFault */
.word 0
.word 0
.word 0
.word 0
.word Default_Handler + 1   /* SVCall */
.word Default_Handler + 1   /* DebugMonitor */
.word 0
.word Default_Handler + 1   /* PendSV */
.word Default_Handler + 1   /* SysTick */
.rept 242
.word Default_Handler + 1
.endr
__pbl_vectors_end:

.section .text.Reset_Handler, "ax", %progbits
.global Reset_Handler
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =__stack_top
    msr msp, r0
    /* VTOR = PBL vector table (SBAF already started us there). */
    ldr r0, =0xE000ED08
    ldr r1, =__pbl_vector_start
    str r1, [r0]
    /* Enable DTCM/ITCM and clear RETEN (startup_cm7.s sequence). */
    ldr r1, =0xE000EF94
    ldr r0, [r1]
    bic r0, r0, #0x4
    orr r0, r0, #0x1
    str r0, [r1]
    ldr r1, =0xE000EF90
    ldr r0, [r1]
    bic r0, r0, #0x4
    orr r0, r0, #0x1
    str r0, [r1]
    dsb
    isb
    /* ECC initialisation with 64-bit stores before any read: the PBL DTCM
     * window (stack) and the PBL SRAM window. Nothing outside the windows is
     * touched, in particular not the C0 standby-retained SRAM. */
    movs r0, #0
    movs r1, #0
    ldr r2, =__pbl_dtcm_start
    ldr r3, =__pbl_dtcm_end
1:  cmp r2, r3
    bcs 2f
    strd r0, r1, [r2], #8
    b 1b
2:  ldr r2, =__pbl_sram_start
    ldr r3, =__pbl_sram_end
3:  cmp r2, r3
    bcs 4f
    strd r0, r1, [r2], #8
    b 3b
    /* .data and .ramfunc from flash (.bss is already zero). */
4:  ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
5:  cmp r1, r2
    bcs 6f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 5b
6:  dsb
    isb
    bl pbl_entry
7:  wfi
    b 7b

.global Default_Handler
.thumb_func
Default_Handler:
    cpsid i
8:  wfi
    b 8b
