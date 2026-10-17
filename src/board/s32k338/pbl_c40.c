/* C40 program/erase jobs executed from SRAM (Q08 "C40 RAM driver").
 *
 * Sequence per RTD C40_Ip.c (MainInterfaceWrite/SectorErase + HVJobStatus):
 *   clear MCRS error flags -> PEADR (PFCPGM_PEADR_L) -> check MCR.PEID ->
 *   DATA[] (program) or DATA[0]=0 (erase) -> MCR.PGM/ERS -> MCR.EHV ->
 *   wait MCRS.DONE -> PEG set, PEP/PES clear -> clear EHV -> clear PGM/ERS.
 * Callers must have passed pbl_flash_program_allowed / erase_allowed and keep
 * interrupts disabled; this file performs no range policy of its own beyond
 * alignment. S32K338 has a single PFLASH/FLASH instance (no MULTIPFLASH).
 *
 * TODO(M10/C40): PBL_C40_DOMAIN_ID (Mem_43_INFLS domain ID of CM7_0) and the
 * sector lock map (PFCBLK_SPELOCK/SSPELOCK) are not configured; without the
 * domain ID every job fails closed, a locked sector fails with PEP. */

#include "pbl_hw.h"
#include "pbl_board_cfg.h"
#include "gen/pbl_layout.h"
#include "S32K358_FLASH.h"
#include "S32K358_PFLASH.h"

#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))
#define C40_ERRORS (FLASH_MCRS_EER_MASK | FLASH_MCRS_SBC_MASK | FLASH_MCRS_AEE_MASK | \
                    FLASH_MCRS_EEE_MASK | FLASH_MCRS_RVE_MASK | FLASH_MCRS_RRE_MASK | \
                    FLASH_MCRS_RWE_MASK | FLASH_MCRS_PES_MASK | FLASH_MCRS_PEP_MASK)
#define C40_PAGE 128u   /* C40_IP_WRITE_MAX_SIZE */

static RAMFUNC bool c40_start(uint32_t address)
{
#ifndef PBL_C40_DOMAIN_ID
    (void)address;
    return false;
#else
    if ((IP_FLASH->MCR & (FLASH_MCR_ERS_MASK | FLASH_MCR_PGM_MASK)) != 0u ||
        (IP_FLASH->UT0 & FLASH_UT0_UTE_MASK) != 0u) {
        return false;
    }
    IP_FLASH->MCRS &= C40_ERRORS;          /* w1c, as C40_Ip_ClearAllErrorFlags */
    IP_PFLASH->PFCPGM_PEADR_L = address;
    return ((IP_FLASH->MCR & FLASH_MCR_PEID_MASK) >> FLASH_MCR_PEID_SHIFT) ==
           (uint32_t)PBL_C40_DOMAIN_ID;
#endif
}

static RAMFUNC bool c40_run(uint32_t mode_mask)
{
    uint32_t n, status;
    bool done = false;
    IP_FLASH->MCR |= mode_mask;
    IP_FLASH->MCR |= FLASH_MCR_EHV_MASK;
    for (n = 0u; n < PBL_POLL_LIMIT; ++n) {
        if ((IP_FLASH->MCRS & FLASH_MCRS_DONE_MASK) != 0u) {
            done = true;
            break;
        }
    }
    status = IP_FLASH->MCRS & (FLASH_MCRS_PEG_MASK | FLASH_MCRS_PEP_MASK | FLASH_MCRS_PES_MASK);
    IP_FLASH->MCR &= ~FLASH_MCR_EHV_MASK;
    IP_FLASH->MCR &= ~mode_mask;
    return done && status == FLASH_MCRS_PEG_MASK;
}

bool RAMFUNC pbl_c40_program(uint32_t address, const uint32_t *words, uint32_t length)
{
    uint32_t first, i;
    if (words == 0 || length == 0u || (address & 7u) != 0u || (length & 7u) != 0u ||
        length > C40_PAGE || (address / C40_PAGE) != ((address + length - 1u) / C40_PAGE) ||
        !c40_start(address)) {
        return false;
    }
    first = (address & (C40_PAGE - 1u)) >> 2;
    for (i = 0u; i < (length >> 2); ++i) {
        IP_FLASH->DATA[first + i] = words[i];
    }
    return c40_run(FLASH_MCR_PGM_MASK);
}

bool RAMFUNC pbl_c40_erase_sector(uint32_t address)
{
    if ((address & (PBL_LAYOUT_PFLASH_SECTOR - 1u)) != 0u || !c40_start(address)) {
        return false;
    }
    IP_FLASH->DATA[0] = 0u;
    return c40_run(FLASH_MCR_ERS_MASK);
}
