.syntax unified
.cpu cortex-m7
.thumb

/* Candidate IVT, based on current S32K338 startup. HSE 2.55/SBAF
 * installation and protection must be validated on a target board. */
.section .boot_header, "a", %progbits
.word 0x5AA55AA5
.word 0x00000001
.word 0xFFFFFFFF
.word 0x00402000
.word 0xFFFFFFFF
.word 0xFFFFFFFF
.word 0xFFFFFFFF
.word 0xFFFFFFFF
.word 0xFFFFFFFF
.word 0x00000000
.rept 54
.word 0xFFFFFFFF
.endr

.section .isr_vector, "a", %progbits
.word __stack_top
.word Reset_Handler + 1
.rept 14
.word Default_Handler + 1
.endr

.section .text.Reset_Handler, "ax", %progbits
.global Reset_Handler
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =__bss_start
    ldr r2, =__bss_end
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl pbl_entry
5:  wfi
    b 5b

.global Default_Handler
.thumb_func
Default_Handler:
    cpsid i
6:  wfi
    b 6b
