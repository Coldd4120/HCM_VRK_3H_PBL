#include "pbl_core.h"
#include <stddef.h>

uint32_t pbl_crc32(const void *data, uint32_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0u; i < length; ++i) {
        uint32_t bit;
        crc ^= p[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

void pbl_record_seal(PblRecord *r)
{
    if (r != NULL) {
        r->crc32 = pbl_crc32(r, (uint32_t)offsetof(PblRecord, crc32));
    }
}

bool pbl_record_valid(const PblRecord *r)
{
    if (r == NULL || r->magic != PBL_RECORD_MAGIC ||
        r->version != PBL_RECORD_VERSION ||
        r->confirmed_bank > 1u || r->target_bank > 1u ||
        r->trial_boots > PBL_MAX_TRIAL_BOOTS ||
        r->state < PBL_STATE_CONFIRMED ||
        r->state > PBL_STATE_ROLLING_BACK) {
        return false;
    }
    if (r->state == PBL_STATE_CONFIRMED) {
        if (r->confirmed_bank != r->target_bank || r->trial_boots != 0u ||
            r->confirmed_package_id != r->target_package_id) {
            return false;
        }
    } else if (r->confirmed_bank == r->target_bank) {
        return false;
    }
    return r->crc32 == pbl_crc32(r, (uint32_t)offsetof(PblRecord, crc32));
}

bool pbl_select_record(const PblRecord *a, const PblRecord *b, PblRecord *out)
{
    bool va = pbl_record_valid(a), vb = pbl_record_valid(b);
    if (out == NULL || (!va && !vb)) {
        return false;
    }
    if (va && vb) {
        uint32_t delta = a->sequence - b->sequence;
        if (delta == 0x80000000u) {
            return false;
        }
        if (delta == 0u &&
            (a->crc32 != b->crc32 ||
             a->state != b->state ||
             a->confirmed_bank != b->confirmed_bank ||
             a->target_bank != b->target_bank ||
             a->trial_boots != b->trial_boots ||
             a->confirmed_package_id != b->confirmed_package_id ||
             a->target_package_id != b->target_package_id)) {
            return false;
        }
        *out = (delta == 0u || delta < 0x80000000u) ? *a : *b;
    } else {
        *out = va ? *a : *b;
    }
    return true;
}

static PblResult recover(const PblPort *p)
{
    if (p->recovery != NULL) {
        p->recovery(p->ctx);
    }
    return PBL_RESULT_RECOVERY;
}

static bool persist(const PblPort *p, PblRecord *r, PblState state)
{
    if (p->append_record == NULL) {
        return false;
    }
    r->state = (uint32_t)state;
    r->sequence++;
    pbl_record_seal(r);
    return p->append_record(p->ctx, r);
}

bool pbl_c0_vector_ok(const PblPort *p, uint32_t vector)
{
    uint32_t msp = 0u, entry = 0u;
    /* Unsigned window arithmetic: vector >= base is checked first, so
     * END - vector cannot wrap. */
    if (p == NULL || p->read_u32 == NULL ||
        vector < PBL_LAYOUT_C0_BASE || vector >= PBL_LAYOUT_C0_END ||
        (vector & (PBL_LAYOUT_C0_VECTOR_ALIGN - 1u)) != 0u ||
        (PBL_LAYOUT_C0_END - vector) <= PBL_C0_VECTOR_BYTES) {
        return false;
    }
    if (!p->read_u32(p->ctx, vector, &msp) ||
        !p->read_u32(p->ctx, vector + 4u, &entry)) {
        return false;
    }
    if (msp <= PBL_LAYOUT_RAM_C0_MSP_WINDOW_BASE ||
        msp > PBL_LAYOUT_RAM_C0_MSP_WINDOW_END || (msp & 7u) != 0u) {
        return false;
    }
    if ((entry & 1u) == 0u) {
        return false;
    }
    entry &= ~1u;
    return entry >= (vector + PBL_C0_VECTOR_BYTES) && entry < PBL_LAYOUT_C0_END;
}

static bool trusted_vector(const PblPort *p, uint32_t package, uint32_t *vector)
{
    return p->verify_active_package != NULL &&
           p->verify_active_package(p->ctx, package, vector) &&
           pbl_c0_vector_ok(p, *vector);
}

static PblResult boot(const PblPort *p, uint32_t package)
{
    uint32_t vector = 0u;
    if (p->jump_c0 == NULL || !trusted_vector(p, package, &vector)) {
        return recover(p);
    }
    p->jump_c0(p->ctx, vector);
    return PBL_RESULT_JUMP;
}

static PblResult swap_and_reset(const PblPort *p)
{
    if (p->activate_passive == NULL || p->reset == NULL ||
        !p->activate_passive(p->ctx)) {
        return recover(p);
    }
    p->reset(p->ctx);
    return PBL_RESULT_RESET;
}

static PblResult rollback(const PblPort *p, PblRecord *r)
{
    return persist(p, r, PBL_STATE_ROLLING_BACK)
               ? swap_and_reset(p) : recover(p);
}

PblResult pbl_run(const PblPort *p)
{
    PblRecord a = {0}, b = {0}, r;
    uint32_t bank, vector = 0u;
    if (p == NULL) {
        return PBL_RESULT_RECOVERY;
    }
    if (p->physical_bank == NULL || p->read_record == NULL ||
        !p->physical_bank(p->ctx, &bank) || bank > 1u) {
        return recover(p);
    }
    if (!p->read_record(p->ctx, 0u, &a)) a = (PblRecord){0};
    if (!p->read_record(p->ctx, 1u, &b)) b = (PblRecord){0};
    if (!pbl_select_record(&a, &b, &r)) {
        return recover(p);
    }
    switch ((PblState)r.state) {
    case PBL_STATE_CONFIRMED:
        return bank == r.confirmed_bank
                   ? boot(p, r.confirmed_package_id) : recover(p);
    case PBL_STATE_ACTIVATING:
        if (bank == r.confirmed_bank) {
            /* A failed or interrupted activation keeps the old package. */
            return boot(p, r.confirmed_package_id);
        }
        if (bank != r.target_bank) {
            return recover(p);
        }
        /* Sec.15.6: the activation boot is trial boot 1, persisted with a
         * single TRIAL write before the hand-off. An image that does not
         * authenticate is rolled back without a trial. */
        if (!trusted_vector(p, r.target_package_id, &vector)) {
            return rollback(p, &r);
        }
        r.trial_boots = 1u;
        if (!persist(p, &r, PBL_STATE_TRIAL) || p->jump_c0 == NULL) {
            return recover(p);
        }
        p->jump_c0(p->ctx, vector);
        return PBL_RESULT_JUMP;
    case PBL_STATE_TRIAL:
        if (bank == r.confirmed_bank) {
            r.target_bank = bank;
            r.target_package_id = r.confirmed_package_id;
            r.trial_boots = 0u;
            return persist(p, &r, PBL_STATE_CONFIRMED)
                       ? boot(p, r.confirmed_package_id) : recover(p);
        }
        if (bank != r.target_bank) return recover(p);
        if (r.trial_boots >= PBL_MAX_TRIAL_BOOTS ||
            !trusted_vector(p, r.target_package_id, &vector)) {
            return rollback(p, &r);
        }
        r.trial_boots++;
        if (!persist(p, &r, PBL_STATE_TRIAL) || p->jump_c0 == NULL) {
            return recover(p);
        }
        p->jump_c0(p->ctx, vector);
        return PBL_RESULT_JUMP;
    case PBL_STATE_ROLLING_BACK:
        if (bank == r.confirmed_bank) {
            r.target_bank = bank;
            r.target_package_id = r.confirmed_package_id;
            r.trial_boots = 0u;
            return persist(p, &r, PBL_STATE_CONFIRMED)
                       ? boot(p, r.confirmed_package_id) : recover(p);
        }
        return bank == r.target_bank ? swap_and_reset(p) : recover(p);
    default:
        return recover(p);
    }
}

#ifdef PBL_TXN_BACKEND
#include "txn_store.h"
#include "txn_pbl.h"

/* LH-R1-002/003: TxnRecordV1 boot decision wired over the board journal media
 * and the HSE digest authenticator. Pure glue: the decision table lives in
 * txn_pbl_run(); authentication and the final vector check reuse the existing
 * board port (verify_active_package + pbl_c0_vector_ok). */
typedef struct {
    const PblPort *port;
    const TxnMedia *media;
    int (*auth)(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]);
} PblTxnCtx;

static int pbl_txn_auth_cb(void *ctx, const uint8_t digest[TXN_DIGEST_LEN])
{
    const PblTxnCtx *c = (const PblTxnCtx *)ctx;
    return (c->auth != NULL) ? c->auth(c->port->ctx, digest) : 0;
}

static int pbl_txn_commit_cb(void *ctx, const TxnRecordV1 *next)
{
    const PblTxnCtx *c = (const PblTxnCtx *)ctx;
    return (txn_store_append(c->media, next) == TXN_APPEND_OK) ? 1 : 0;
}

PblResult pbl_run_txn(const PblPort *p, const TxnMedia *media,
                      int (*auth)(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]))
{
    PblTxnCtx cctx;
    TxnLocation loc;
    TxnPblPorts tp;
    TxnPblOutcome outcome;
    uint32_t bank, pkg = 0u, vector = 0u;
    if (p == NULL || media == NULL || p->physical_bank == NULL) {
        return PBL_RESULT_RECOVERY;
    }
    if (!p->physical_bank(p->ctx, &bank) || bank > 1u) {
        return recover(p);
    }
    if (txn_store_load(media, &loc) != TXN_LOAD_OK || loc.valid == 0) {
        return recover(p);
    }
    /* LH-R2-007: a persisted record built for a different layout must never
     * drive a boot/swap/activation decision (detailed design 15.2). Reject it
     * and stay in recovery before any authentication, commit or jump; never
     * infer the current layout from the record itself. */
    if (loc.record.layout_id != PBL_LAYOUT_ID) {
        return recover(p);
    }
    cctx.port = p; cctx.media = media; cctx.auth = auth;
    tp.auth = pbl_txn_auth_cb;
    tp.commit = pbl_txn_commit_cb;
    tp.ctx = &cctx;
    outcome = txn_pbl_run(&loc.record, (uint8_t)bank, &tp);
    switch (outcome) {
    case TXN_PBL_JUMP_CONFIRMED:
        pkg = loc.record.confirmed_package_id;
        break;
    case TXN_PBL_JUMP_TARGET:
        pkg = loc.record.target_package_id;
        break;
    case TXN_PBL_ACTIVATE_RESET:
        return swap_and_reset(p);
    case TXN_PBL_RECOVERY:
    default:
        return recover(p);
    }
    if (!trusted_vector(p, pkg, &vector)) {
        return recover(p);
    }
    p->jump_c0(p->ctx, vector);
    return PBL_RESULT_JUMP;
}
#endif /* PBL_TXN_BACKEND */
