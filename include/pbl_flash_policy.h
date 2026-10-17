#ifndef PBL_FLASH_POLICY_H
#define PBL_FLASH_POLICY_H

/* Recovery flash whitelist (Q09). Only the passive copies of descriptor,
 * C0, C1 and C2 may ever be erased or programmed by the PBL; never the PBL
 * copy, the spare area, either HSE tail or anything in the active half.
 * Program requests must be double-word aligned and stay inside one 128-byte
 * C40 program page (C40_IP_WRITE_MAX_SIZE); erases are whole sectors. */

#include <stdbool.h>
#include <stdint.h>

#define PBL_FLASH_PROGRAM_UNIT 8u     /* C40 double word */
#define PBL_FLASH_PAGE_BYTES   128u   /* C40_IP_WRITE_MAX_SIZE */

bool pbl_flash_program_allowed(uint32_t address, uint32_t length);
bool pbl_flash_erase_allowed(uint32_t address, uint32_t length);

#endif
