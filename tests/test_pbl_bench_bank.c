/* S1 host test: bench PBL boot bank from DCMSTAT (PBL_BENCH_DCM_BANK).
 * include/pbl_bench_bank.h and the shared MAIN vrk_s32k338_bank.c are compiled
 * byte for byte; pbl_run() is the production state machine. The board port is
 * modelled exactly like pbl_board_s32k338.c under PBL_BENCH_DCM_BANK:
 * physical_bank() decodes DCMSTAT, read_record() synthesizes CONFIRMED for the
 * observed half, copy 1 absent, no journal write, no HSE request.
 * DCMSTAT values are the measured ones (AB_ACTIVATION_20261022_JLINK69654583):
 * A 0x00010FF1, B 0x00030FF1. */
#include "pbl_bench_bank.h"
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

typedef struct {
    uint32_t dcmstat, bank;
    bool bank_valid;
    int legacy_bank0;     /* 1: model the pre-S1 synthesized confirmed_bank=0 */
    int jumps, swaps, resets, recoveries, writes;
    uint32_t vector;
} Board;

static bool physical_bank(void *ctx, uint32_t *bank)
{
    Board *b = (Board *)ctx;
    b->bank_valid = pbl_bench_dcm_bank(b->dcmstat, &b->bank);
    if (!b->bank_valid) return false;
    *bank = b->bank;
    return true;
}
static bool read_record(void *ctx, uint32_t copy, PblRecord *r)
{
    Board *b = (Board *)ctx;
    if (r == NULL || copy != 0u || !b->bank_valid) return false;
    return pbl_bench_confirmed_record(b->legacy_bank0 ? 0u : b->bank, r);
}
static bool append(void *ctx, const PblRecord *r) { (void)r; ((Board *)ctx)->writes++; return false; }
static bool verify(void *ctx, uint32_t pkg, uint32_t *v) { (void)ctx; (void)pkg; *v = PBL_LAYOUT_C0_BASE; return true; }
static bool activate(void *ctx) { ((Board *)ctx)->swaps++; return true; }
static void jump(void *ctx, uint32_t v) { ((Board *)ctx)->jumps++; ((Board *)ctx)->vector = v; }
static void reset(void *ctx) { ((Board *)ctx)->resets++; }
static void recovery(void *ctx) { ((Board *)ctx)->recoveries++; }
static bool read_u32(void *ctx, uint32_t a, uint32_t *v)
{
    (void)ctx;
    if (a == PBL_LAYOUT_C0_BASE) { *v = PBL_LAYOUT_RAM_C0_MSP_WINDOW_END; return true; }
    if (a == PBL_LAYOUT_C0_BASE + 4u) { *v = (PBL_LAYOUT_C0_BASE + 0x420u) | 1u; return true; }
    return false;
}

static PblResult boot_once(Board *b)
{
    PblPort p = {b, physical_bank, read_record, append, verify, activate, jump, reset, recovery, read_u32};
    b->jumps = b->swaps = b->resets = b->recoveries = b->writes = 0;
    return pbl_run(&p);
}

int main(void)
{
    Board b = {0};
    uint32_t bank = 7u;
    PblRecord r;
    static const uint32_t bad[] = {
        0x00000000u,  /* DCM not run */
        0x00000FF1u,  /* OTA not active (not AB_SWAP) */
        0x00010FF0u,  /* DCM not done */
        0x00010DF1u,  /* OTA scan not OK (bit9 clear) */
        0x00030DF0u,  /* B high bit but scan/done missing */
        0xFFFFFFFEu,  /* all set except DONE */
    };
    unsigned i;

    CHECK(pbl_bench_dcm_bank(0x00010FF1u, &bank) && bank == 0u);
    CHECK(pbl_bench_dcm_bank(0x00030FF1u, &bank) && bank == 1u);
    CHECK(!pbl_bench_dcm_bank(0x00010FF1u, NULL));
    for (i = 0u; i < sizeof bad / sizeof bad[0]; ++i) {
        bank = 7u;
        CHECK(!pbl_bench_dcm_bank(bad[i], &bank) && bank == 7u);
    }
    CHECK(pbl_bench_confirmed_record(1u, &r) && pbl_record_valid(&r) &&
          r.state == PBL_STATE_CONFIRMED && r.confirmed_bank == 1u && r.target_bank == 1u);
    CHECK(!pbl_bench_confirmed_record(2u, &r));
    CHECK(!pbl_bench_confirmed_record(0u, NULL));
    printf("PASS bench DCM decode: A=0x00010FF1->0, B=0x00030FF1->1, %u invalid rejected\n",
           (unsigned)(sizeof bad / sizeof bad[0]));

    /* Simulated A -> B -> A (HSE swap + reset happen outside the PBL). */
    b.dcmstat = 0x00010FF1u;
    CHECK(boot_once(&b) == PBL_RESULT_JUMP && b.jumps == 1 && b.vector == PBL_LAYOUT_C0_BASE && b.bank == 0u);
    CHECK(b.swaps == 0 && b.resets == 0 && b.writes == 0 && b.recoveries == 0);
    b.dcmstat = 0x00030FF1u;
    CHECK(boot_once(&b) == PBL_RESULT_JUMP && b.jumps == 1 && b.bank == 1u);
    CHECK(b.swaps == 0 && b.resets == 0 && b.writes == 0 && b.recoveries == 0);
    b.dcmstat = 0x00010FF1u;
    CHECK(boot_once(&b) == PBL_RESULT_JUMP && b.jumps == 1 && b.bank == 0u);
    printf("PASS bench boot A->B->A: C0 started on bank 0, 1, 0; no swap/reset/journal write\n");

    /* Why S1 is needed: the pre-S1 record (confirmed_bank=0) on half B. */
    b.legacy_bank0 = 1; b.dcmstat = 0x00030FF1u;
    CHECK(boot_once(&b) == PBL_RESULT_RECOVERY && b.jumps == 0 && b.recoveries == 1 && b.swaps == 0);
    b.legacy_bank0 = 0;
    printf("PASS pre-S1 model on B (confirmed_bank=0) ends in recovery (the defect S1 removes)\n");

    /* Failure path: undecodable DCMSTAT never falls back to bank 0. */
    for (i = 0u; i < sizeof bad / sizeof bad[0]; ++i) {
        b.dcmstat = bad[i];
        CHECK(boot_once(&b) == PBL_RESULT_RECOVERY && b.jumps == 0 && b.recoveries == 1);
        CHECK(b.swaps == 0 && b.resets == 0 && b.writes == 0);
    }
    printf("PASS undecodable DCMSTAT -> recovery, no jump/swap/reset/write (%u cases)\n",
           (unsigned)(sizeof bad / sizeof bad[0]));
    printf("PASS bench DCM bank suite\n");
    return 0;
}