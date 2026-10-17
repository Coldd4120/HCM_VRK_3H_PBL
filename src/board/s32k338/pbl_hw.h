#ifndef PBL_HW_H
#define PBL_HW_H

/* S32K338 register-level services for the PBL (target only). */

#include <stdbool.h>
#include <stdint.h>

bool pbl_hw_safe_outputs(void);          /* HSS off, verified */
void pbl_hw_swt_start(void);
void pbl_hw_swt_service(void);
uint32_t pbl_hw_now_ms(void);            /* monotonic ms, SysTick (LH-R1-006) */
void pbl_hw_mpu_default(void);           /* MPU off = default memory map */
bool pbl_hw_can0_start(void);            /* false while the clock is TODO */
bool pbl_hw_can0_receive(uint32_t *id, uint8_t *data, uint32_t *dlc);
bool pbl_hw_can0_send(uint32_t id, const uint8_t *data);
bool pbl_hw_hse_get_attr(uint32_t attr_id, void *out, uint32_t length);
bool pbl_hw_hse_activate_passive(void);
void pbl_hw_functional_reset(void) __attribute__((noreturn));
void pbl_hw_jump(uint32_t vector) __attribute__((noreturn));

/* C40 high-voltage jobs, executed from SRAM (.ramfunc). */
bool pbl_c40_program(uint32_t address, const uint32_t *words, uint32_t length);
bool pbl_c40_erase_sector(uint32_t address);

#endif
