#include "txn_store.h"
#include <string.h>

static int slot_is_blank(const uint8_t b[TXN_RECORD_SIZE])
{
    uint32_t i;
    for (i = 0u; i < TXN_RECORD_SIZE; i++) {
        if (b[i] != 0xFFu) { return 0; }
    }
    return 1;
}

TxnLoadStatus txn_store_load(const TxnMedia *m, TxnLocation *out)
{
    uint8_t buf[TXN_RECORD_SIZE];
    TxnRecordV1 best;
    uint8_t best_bytes[TXN_RECORD_SIZE];
    uint32_t best_unit = 0u, best_slot = 0u;
    int have_best = 0;
    int any_nonblank = 0;
    int corrupt = 0;
    uint32_t u, s;

    out->valid = 0;
    for (u = 0u; u < m->units; u++) {
        for (s = 0u; s < m->slots_per_unit; s++) {
            TxnRecordV1 r;
            if (m->read(m->ctx, u, s, buf) != 0) {
                return TXN_LOAD_HW_ERROR;
            }
            if (!slot_is_blank(buf)) {
                any_nonblank = 1;
            }
            if (!txn_record_valid(buf, &r)) {
                continue;
            }
            if (!have_best) {
                best = r; memcpy(best_bytes, buf, TXN_RECORD_SIZE);
                best_unit = u; best_slot = s; have_best = 1;
            } else {
                int c = txn_serial_cmp(r.sequence, best.sequence);
                if (c == TXN_SERIAL_AMBIGUOUS) {
                    corrupt = 1;
                } else if (c > 0) {
                    best = r; memcpy(best_bytes, buf, TXN_RECORD_SIZE);
                    best_unit = u; best_slot = s;
                } else if (c == 0) {
                    /* same sequence: content must be identical, else corrupt */
                    if (memcmp(buf, best_bytes, TXN_RECORD_SIZE) != 0) {
                        corrupt = 1;
                    }
                }
            }
        }
    }

    if (corrupt) {
        return TXN_LOAD_CORRUPT;
    }
    if (!have_best) {
        return any_nonblank ? TXN_LOAD_CORRUPT : TXN_LOAD_EMPTY;
    }
    out->record = best;
    out->unit = best_unit;
    out->slot = best_slot;
    out->valid = 1;
    return TXN_LOAD_OK;
}

static TxnAppendStatus program_slot(const TxnMedia *m, uint32_t unit, uint32_t slot,
                                    const TxnRecordV1 *rec)
{
    uint8_t enc[TXN_RECORD_SIZE];
    uint8_t rb[TXN_RECORD_SIZE];

    txn_encode_body(rec, enc);               /* body + crc + reserved_ff        */
    if (m->program(m->ctx, unit, slot, 0u, enc, TXN_COMMIT_OFFSET) != 0) {
        return TXN_APPEND_HW_ERROR;
    }
    if (m->read(m->ctx, unit, slot, rb) != 0) {
        return TXN_APPEND_HW_ERROR;
    }
    if (memcmp(rb, enc, TXN_COMMIT_OFFSET) != 0) {
        return TXN_APPEND_VERIFY_FAIL;        /* body read-back mismatch         */
    }
    txn_fill_commit(enc);                      /* now set commit unit             */
    if (m->program(m->ctx, unit, slot, TXN_COMMIT_OFFSET,
                   &enc[TXN_COMMIT_OFFSET], TXN_RECORD_SIZE - TXN_COMMIT_OFFSET) != 0) {
        return TXN_APPEND_HW_ERROR;
    }
    if (m->read(m->ctx, unit, slot, rb) != 0) {
        return TXN_APPEND_HW_ERROR;
    }
    if (!txn_record_valid(rb, NULL)) {
        return TXN_APPEND_VERIFY_FAIL;
    }
    return TXN_APPEND_OK;
}

static int find_free_slot(const TxnMedia *m, uint32_t unit, uint32_t from,
                          uint32_t *out_slot)
{
    uint8_t buf[TXN_RECORD_SIZE];
    uint32_t s;
    for (s = from; s < m->slots_per_unit; s++) {
        if (m->read(m->ctx, unit, s, buf) != 0) {
            return -1;
        }
        if (slot_is_blank(buf)) {
            *out_slot = s;
            return 1;
        }
    }
    return 0; /* full */
}

TxnAppendStatus txn_store_append(const TxnMedia *m, const TxnRecordV1 *rec)
{
    TxnLocation cur;
    TxnLoadStatus ls = txn_store_load(m, &cur);
    uint32_t unit, slot;

    if (ls == TXN_LOAD_HW_ERROR) {
        return TXN_APPEND_HW_ERROR;
    }
    if (ls == TXN_LOAD_EMPTY) {
        /* fresh medium: unit 0 slot 0 */
        return program_slot(m, 0u, 0u, rec);
    }
    if (ls == TXN_LOAD_CORRUPT) {
        /* Caller is expected to drive RECOVERY; refuse to append over corruption. */
        return TXN_APPEND_FULL_ERROR;
    }

    unit = cur.unit;
    {
        int r = find_free_slot(m, unit, cur.slot + 1u, &slot);
        if (r < 0) {
            return TXN_APPEND_HW_ERROR;
        }
        if (r == 1) {
            return program_slot(m, unit, slot, rec);
        }
    }
    /* current unit is full: switch to the other unit (section 15.4). */
    {
        uint32_t other = (unit == 0u) ? 1u : 0u;
        TxnAppendStatus as;
        if (m->erase(m->ctx, other) != 0) {
            return TXN_APPEND_HW_ERROR;
        }
        as = program_slot(m, other, 0u, rec);   /* migrated latest, seq already +1 */
        if (as != TXN_APPEND_OK) {
            return as;
        }
        if (m->erase(m->ctx, unit) != 0) {        /* erase old unit (W6->W7)        */
            return TXN_APPEND_HW_ERROR;
        }
        return TXN_APPEND_OK;
    }
}
