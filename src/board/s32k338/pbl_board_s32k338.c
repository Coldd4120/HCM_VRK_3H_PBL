/* S32K338 board binding of the PBL port (Q08).
 *
 * Bound and working: safe outputs, SWT, MPU default, jump to C0 (VTOR/MSP,
 * IRQs off and cleared), functional reset, HSE MU request path, C40 SRAM
 * driver, CAN0 + recovery UDS loop.
 * Fail closed until resolved (each is listed in the stage-3 report):
 *   physical_bank       TODO(M09/NXP) no verified HSE source for the A/B block
 *   read/append_record  TODO(Q07) D-Flash txn unit offsets are null in layout
 *   verify_active_pkg   TODO(Q07) descriptor format + HSE SMR verification
 *   CAN0                TODO(clock) FLEXCANA clock tree not configured
 * With these TODOs open the PBL always ends in recovery (never runs C0). */

#include "pbl_board.h"
#include "pbl_uds.h"
#include "pbl_board_cfg.h"
#include "pbl_hw.h"
#include "gen/pbl_layout.h"

#include <stddef.h>

#ifdef PBL_PRODUCTION
#if PBL_LAYOUT_APPROVED != 1u
#error "PBL_PRODUCTION requires an approved layout (layout.json approved=true)"
#endif
#error "PBL_PRODUCTION: s32k338 port still has fail-closed TODOs (bank, record, verify, CAN clock)"
#endif

#define PBL_VERSION 0x00030000u   /* stage 3 port, 0xMMmmpppp */

enum {
    PBL_REASON_NONE = 0u,
    PBL_REASON_UNSAFE_OUTPUTS = 1u,
    PBL_REASON_BOOT_FAILED = 2u
};

static uint8_t recovery_reason = PBL_REASON_BOOT_FAILED;

static bool board_physical_bank(void *ctx, uint32_t *bank)
{
    (void)ctx;
#ifdef PBL_HSE_BANK_FROM_FW_PARTITION_VERIFIED
    uint8_t partition = 0u;
    if (!pbl_hw_hse_get_attr(PBL_HSE_FW_PARTITION_ATTR_ID, &partition, 1u)) {
        return false;
    }
    if (partition == PBL_HSE_FW_PARTITION_PRIMARY) { *bank = 0u; return true; }
    if (partition == PBL_HSE_FW_PARTITION_BACKUP)  { *bank = 1u; return true; }
    return false;
#else
    (void)bank;
    return false;
#endif
}

static bool board_read_record(void *ctx, uint32_t copy, PblRecord *record)
{
    (void)ctx; (void)copy; (void)record;
    return false;   /* TODO(Q07): D-Flash txn units (layout dflash.txn_units) */
}

static bool board_append_record(void *ctx, const PblRecord *record)
{
    (void)ctx; (void)record;
    return false;   /* TODO(Q07): append + read back + verify on D-Flash */
}

static bool board_verify(void *ctx, uint32_t package, uint32_t *c0_vector)
{
    (void)ctx; (void)package; (void)c0_vector;
    return false;   /* TODO(Q07): descriptor + HSE SMR verify of the active half */
}

static bool board_activate(void *ctx)
{
    (void)ctx;
    return pbl_hw_hse_activate_passive();
}

static bool board_read_u32(void *ctx, uint32_t address, uint32_t *value)
{
    (void)ctx;
    /* Only the active C0 region is readable through this port. */
    if (address < PBL_LAYOUT_C0_BASE || address > (PBL_LAYOUT_C0_END - 4u) ||
        (address & 3u) != 0u) {
        return false;
    }
    *value = *(const volatile uint32_t *)(uintptr_t)address;
    return true;
}

static void board_jump(void *ctx, uint32_t vector)
{
    (void)ctx;
    pbl_hw_jump(vector);
}

static void board_reset(void *ctx)
{
    (void)ctx;
    pbl_hw_functional_reset();
}

static void serve_frame(PblUds *uds, uint32_t id, const uint8_t *data, uint32_t dlc,
                        uint32_t now_ms)
{
    const uint8_t *payload = NULL;
    uint8_t rsp[8], frame[8];
    uint32_t n, tx;
    bool functional = (id == PBL_UDS_FUNC_RX_ID);
    if (id == PBL_UDS_LH_RX_ID || functional) {
        tx = PBL_UDS_LH_TX_ID;
    } else if (id == PBL_UDS_RH_RX_ID) {
        tx = PBL_UDS_RH_TX_ID;
    } else {
        return;
    }
    n = pbl_isotp_sf_decode(data, dlc, &payload);
    if (n == 0u) {
        return;
    }
    n = pbl_uds_process(uds, payload, n, functional, now_ms, rsp, sizeof(rsp));
    if (n != 0u && pbl_isotp_sf_encode(rsp, n, frame, PBL_UDS_PADDING)) {
        (void)pbl_hw_can0_send(tx, frame);
    }
}

static void board_recovery(void *ctx)
{
    PblUdsInfo info = {PBL_LAYOUT_ID, PBL_VERSION, 0u, 0xFFu, 0u};
    PblUds uds;
    uint32_t id, dlc;
    uint8_t data[8];
    bool can;
    (void)ctx;
    info.recovery_reason = recovery_reason;
    (void)pbl_hw_safe_outputs();
    pbl_uds_init(&uds, &info, pbl_hw_now_ms());
    can = pbl_hw_can0_start();
    for (;;) {
        uint32_t now_ms = pbl_hw_now_ms();
        pbl_hw_swt_service();
        /* LH-R1-006: the monotonic SysTick base drives the S3 session. */
        if (can && pbl_hw_can0_receive(&id, data, &dlc)) {
            serve_frame(&uds, id, data, dlc, now_ms);
        }
        pbl_uds_tick(&uds, now_ms);
        if (uds.reset_requested) {
            pbl_hw_functional_reset();
        }
    }
}

const PblPort pbl_board_port = {
    .ctx = 0,
    .physical_bank = board_physical_bank,
    .read_record = board_read_record,
    .append_record = board_append_record,
    .verify_active_package = board_verify,
    .activate_passive = board_activate,
    .jump_c0 = board_jump,
    .reset = board_reset,
    .recovery = board_recovery,
    .read_u32 = board_read_u32
};

void pbl_board_init(void)
{
    /* Safe outputs before anything else; a failure is remembered and the
     * PBL refuses to start C0 with unverified HSS outputs. */
    if (!pbl_hw_safe_outputs()) {
        recovery_reason = PBL_REASON_UNSAFE_OUTPUTS;
        pbl_hw_swt_start();
        board_recovery(0);
    }
    pbl_hw_mpu_default();
    pbl_hw_swt_start();
}

#ifdef PBL_TXN_BACKEND
#include "txn_store.h"
#include "txn_pbl.h"

/* LH-R1-002: board binding of the TxnRecordV1 journal + image authenticator.
 * All of it is gated by PBL_TXN_BACKEND and fails closed: with the hardware
 * parameters below unconfirmed, txn_store_load / pbl_board_txn_auth return an
 * error and pbl_run_txn ends in recovery (never starts C0). The production
 * audit still blocks via the PBL_PRODUCTION #error above.
 *
 * Bank identification: board_physical_bank() (above) already reads the A/B
 * block from HSE, gated by PBL_HSE_BANK_FROM_FW_PARTITION_VERIFIED (M09).
 *
 * Download path: the UDS programming sequence (0x34/0x36/0x37) writes through
 * pbl_flash_policy (erase/program whitelist) + pbl_c40; it stays disabled in
 * pbl_uds.c and fails closed until the C40 domain ID (M10) and the measured
 * D-Flash/P-Flash program units are confirmed. TODO(å¾ç¡®è®¤). */
#if defined(PBL_TXN_DFLASH_UNIT0_BASE) && defined(PBL_TXN_DFLASH_UNIT1_BASE) && \
    defined(PBL_TXN_DFLASH_SLOTS)
/* Real media: D-Flash is memory-mapped for reads; writes/erase go via C40.
 * TODO(å¾ç¡®è®¤ Q07): the unit bases/slot count come from layout dflash.txn_units
 * (null today) and must be measured before this branch is enabled. */
static const uint32_t txn_unit_base[TXN_UNIT_COUNT] = {
    PBL_TXN_DFLASH_UNIT0_BASE, PBL_TXN_DFLASH_UNIT1_BASE };
static int board_txn_read(void *ctx, uint32_t unit, uint32_t slot, uint8_t out[TXN_RECORD_SIZE]) {
    const volatile uint8_t *src; uint32_t i; (void)ctx;
    if (unit >= TXN_UNIT_COUNT) { return -1; }
    src = (const volatile uint8_t *)(uintptr_t)(txn_unit_base[unit] + slot * TXN_RECORD_SIZE);
    for (i = 0u; i < TXN_RECORD_SIZE; ++i) { out[i] = src[i]; }
    return 0;
}
static int board_txn_program(void *ctx, uint32_t unit, uint32_t slot, uint32_t off,
                             const uint8_t *src, uint32_t len) {
    uint32_t addr; (void)ctx;
    if (unit >= TXN_UNIT_COUNT) { return -1; }
    addr = txn_unit_base[unit] + slot * TXN_RECORD_SIZE + off;
    return pbl_c40_program(addr, (const uint32_t *)(const void *)src, len) ? 0 : -1;
}
static int board_txn_erase(void *ctx, uint32_t unit) {
    (void)ctx;
    if (unit >= TXN_UNIT_COUNT) { return -1; }
    return pbl_c40_erase_sector(txn_unit_base[unit]) ? 0 : -1;
}
static const TxnMedia board_txn_media_obj = {
    board_txn_read, board_txn_program, board_txn_erase, NULL,
    PBL_TXN_DFLASH_SLOTS, TXN_UNIT_COUNT };
#else
/* Fail-closed media: no verified D-Flash txn units yet (Q07 / å¾ç¡®è®¤). */
static int board_txn_read(void *ctx, uint32_t unit, uint32_t slot, uint8_t out[TXN_RECORD_SIZE]) {
    (void)ctx; (void)unit; (void)slot; (void)out; return -1;
}
static int board_txn_program(void *ctx, uint32_t unit, uint32_t slot, uint32_t off,
                             const uint8_t *src, uint32_t len) {
    (void)ctx; (void)unit; (void)slot; (void)off; (void)src; (void)len; return -1;
}
static int board_txn_erase(void *ctx, uint32_t unit) { (void)ctx; (void)unit; return -1; }
static const TxnMedia board_txn_media_obj = {
    board_txn_read, board_txn_program, board_txn_erase, NULL,
    TXN_SLOTS_PER_UNIT, TXN_UNIT_COUNT };
#endif

const TxnMedia *pbl_board_txn_media(void) { return &board_txn_media_obj; }

int pbl_board_txn_auth(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]) {
    (void)ctx; (void)digest;
    /* LH-R1-002: image verification of the active package by HSE SMR over the
     * TxnRecordV1 digest. The HSE SMR verify service format is not confirmed for
     * this project (Q07 / å¾ç¡®è®¤); until then authentication fails closed and the
     * PBL ends in recovery rather than trusting an unverified image. */
    return 0;
}
#endif /* PBL_TXN_BACKEND */
