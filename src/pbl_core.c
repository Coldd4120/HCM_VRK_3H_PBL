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

static bool trusted_vector(const PblPort *p, uint32_t package, uint32_t *vector)
{
    return p->verify_active_package != NULL &&
           p->verify_active_package(p->ctx, package, vector) &&
           *vector >= 0x00440000u && *vector < 0x007D4000u &&
           (*vector & 0x1FFu) == 0u;
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
        if (bank != r.target_bank ||
            !persist(p, &r, PBL_STATE_TRIAL)) {
            return recover(p);
        }
        /* Count the first trial boot before handing off. */
        /* fall through */
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
