/* S1 (PBL_BENCH_DCM_BANK, bench scope only, FS-06 bench sign-off 2026-10-22):
 * the bench PBL reports the physical A/B half observed in DCMSTAT instead of the
 * fixed bank 0, and its synthesized CONFIRMED record follows that observed half,
 * so a chip that HSE swapped to block B boots C0 there instead of recovery.
 * A DCMSTAT that does not decode (DCM not done, OTA scan not OK, OTA not active)
 * never falls back to bank 0: the caller ends in recovery.
 * Pure functions, compiled byte for byte by the host test (tests/test_pbl_bench_bank.c).
 * No register write, no HSE request, no journal write. */
#ifndef PBL_BENCH_BANK_H
#define PBL_BENCH_BANK_H

#include "pbl_core.h"
#include "vrk_s32k338_bank.h"

#include <stddef.h>

static inline bool pbl_bench_dcm_bank(uint32_t dcmstat, uint32_t *bank)
{
    uint8_t observed = 0u;
    if (bank == NULL || Vrk_S32k338Bank_Decode(dcmstat, 1, &observed) != 1) {
        return false;
    }
    *bank = (uint32_t)observed;
    return true;
}

static inline bool pbl_bench_confirmed_record(uint32_t bank, PblRecord *record)
{
    PblRecord r = {0};
    if (record == NULL || bank > 1u) {
        return false;
    }
    r.magic = PBL_RECORD_MAGIC;
    r.version = PBL_RECORD_VERSION;
    r.sequence = 1u;
    r.state = (uint32_t)PBL_STATE_CONFIRMED;
    r.confirmed_bank = bank;
    r.target_bank = bank;
    r.trial_boots = 0u;
    r.confirmed_package_id = 0u;
    r.target_package_id = 0u;
    pbl_record_seal(&r);
    *record = r;
    return true;
}

#endif /* PBL_BENCH_BANK_H */
