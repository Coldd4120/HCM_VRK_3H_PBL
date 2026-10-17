/* TxnRecordV1 codec and transition logic. See txn_record.h. Portable C99. */
#include "txn_record.h"
#include <string.h>

uint32_t txn_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0u; i < len; i++) {
        uint32_t b;
        int k;
        crc ^= (uint32_t)data[i];
        for (k = 0; k < 8; k++) {
            b = (crc & 1u);
            crc >>= 1;
            if (b != 0u) {
                crc ^= 0xEDB88320u;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Serialize [0,112) plus CRC at 112; leaves [116,128) to the caller. */
static void encode_body_no_commit(const TxnRecordV1 *rec, uint8_t out[TXN_RECORD_SIZE])
{
    memset(out, 0, TXN_RECORD_SIZE);
    put_u32(&out[0], TXN_MAGIC);
    put_u16(&out[4], rec->schema);
    put_u16(&out[6], rec->length);
    put_u32(&out[8], rec->sequence);
    put_u32(&out[12], rec->txn_generation);
    out[16] = rec->state;
    out[17] = rec->writer;
    out[18] = rec->confirmed_bank;
    out[19] = rec->target_bank;
    out[20] = rec->attempts;
    out[21] = rec->max_attempts;
    out[22] = rec->result;
    out[23] = rec->flags;
    put_u32(&out[24], rec->confirmed_package_id);
    put_u32(&out[28], rec->target_package_id);
    put_u32(&out[32], rec->layout_id);
    put_u32(&out[36], rec->confirmed_security_version);
    put_u32(&out[40], rec->target_security_version);
    put_u32(&out[44], rec->reserved);
    memcpy(&out[48], rec->confirmed_digest, TXN_DIGEST_LEN);
    memcpy(&out[80], rec->target_digest, TXN_DIGEST_LEN);
    put_u32(&out[TXN_CRC_OFFSET], txn_crc32(out, TXN_CRC_OFFSET));
}

void txn_encode_body(const TxnRecordV1 *rec, uint8_t out[TXN_RECORD_SIZE])
{
    encode_body_no_commit(rec, out);
    put_u32(&out[116], TXN_RESERVED_FF);
    /* commit unit [120,128) left as 0xFF (unprogrammed). */
    out[120] = 0xFFu; out[121] = 0xFFu; out[122] = 0xFFu; out[123] = 0xFFu;
    out[124] = 0xFFu; out[125] = 0xFFu; out[126] = 0xFFu; out[127] = 0xFFu;
}

void txn_fill_commit(uint8_t out[TXN_RECORD_SIZE])
{
    put_u32(&out[120], TXN_COMMIT_WORD);
    put_u32(&out[124], TXN_COMMIT_INV_WORD);
}

void txn_encode(const TxnRecordV1 *rec, uint8_t out[TXN_RECORD_SIZE])
{
    txn_encode_body(rec, out);
    txn_fill_commit(out);
}

void txn_decode(const uint8_t in[TXN_RECORD_SIZE], TxnRecordV1 *rec)
{
    rec->magic = get_u32(&in[0]);
    rec->schema = get_u16(&in[4]);
    rec->length = get_u16(&in[6]);
    rec->sequence = get_u32(&in[8]);
    rec->txn_generation = get_u32(&in[12]);
    rec->state = in[16];
    rec->writer = in[17];
    rec->confirmed_bank = in[18];
    rec->target_bank = in[19];
    rec->attempts = in[20];
    rec->max_attempts = in[21];
    rec->result = in[22];
    rec->flags = in[23];
    rec->confirmed_package_id = get_u32(&in[24]);
    rec->target_package_id = get_u32(&in[28]);
    rec->layout_id = get_u32(&in[32]);
    rec->confirmed_security_version = get_u32(&in[36]);
    rec->target_security_version = get_u32(&in[40]);
    rec->reserved = get_u32(&in[44]);
    memcpy(rec->confirmed_digest, &in[48], TXN_DIGEST_LEN);
    memcpy(rec->target_digest, &in[80], TXN_DIGEST_LEN);
}

TxnStatus txn_validate_struct(const uint8_t in[TXN_RECORD_SIZE])
{
    uint32_t crc;
    if (get_u32(&in[0]) != TXN_MAGIC) {
        return TXN_ERR_MAGIC;
    }
    if (get_u16(&in[4]) != TXN_SCHEMA_V1) {
        return TXN_ERR_SCHEMA;
    }
    if (get_u16(&in[6]) != TXN_LENGTH_V1) {
        return TXN_ERR_LENGTH;
    }
    crc = txn_crc32(in, TXN_CRC_OFFSET);
    if (get_u32(&in[TXN_CRC_OFFSET]) != crc) {
        return TXN_ERR_CRC;
    }
    if ((get_u32(&in[120]) != TXN_COMMIT_WORD) ||
        (get_u32(&in[124]) != TXN_COMMIT_INV_WORD)) {
        return TXN_ERR_COMMIT;
    }
    return TXN_OK;
}

static int digests_equal(const uint8_t *a, const uint8_t *b)
{
    return (memcmp(a, b, TXN_DIGEST_LEN) == 0) ? 1 : 0;
}

TxnStatus txn_check_invariants(const TxnRecordV1 *rec)
{
    switch (rec->state) {
    case TXN_ST_CONFIRMED:
        if ((rec->target_bank != rec->confirmed_bank) ||
            (!digests_equal(rec->confirmed_digest, rec->target_digest)) ||
            (rec->target_package_id != rec->confirmed_package_id) ||
            (rec->attempts != 0u)) {
            return TXN_ERR_INVARIANT;
        }
        return TXN_OK;
    case TXN_ST_PREPARED:
    case TXN_ST_SWAP_REQUESTED:
    case TXN_ST_TRIAL:
    case TXN_ST_ROLLBACK_REQUESTED:
        if ((rec->target_bank == rec->confirmed_bank) ||
            (digests_equal(rec->confirmed_digest, rec->target_digest)) ||
            (rec->attempts > rec->max_attempts)) {
            return TXN_ERR_INVARIANT;
        }
        return TXN_OK;
    case TXN_ST_RECOVERY:
        return TXN_OK;
    default:
        return TXN_ERR_INVARIANT;
    }
}

int txn_record_valid(const uint8_t in[TXN_RECORD_SIZE], TxnRecordV1 *out_opt)
{
    TxnRecordV1 tmp;
    if (txn_validate_struct(in) != TXN_OK) {
        return 0;
    }
    txn_decode(in, &tmp);
    if (txn_check_invariants(&tmp) != TXN_OK) {
        return 0;
    }
    if (out_opt != NULL) {
        *out_opt = tmp;
    }
    return 1;
}

int txn_serial_cmp(uint32_t a, uint32_t b)
{
    uint32_t diff;
    if (a == b) {
        return 0;
    }
    diff = (uint32_t)(a - b);
    if (diff == 0x80000000u) {
        return TXN_SERIAL_AMBIGUOUS;
    }
    return (diff < 0x80000000u) ? 1 : -1;
}

/* ------------------------------------------------------------------------- */
/* Transition permission table (section 15.3).                               */

static int bank_is_other(uint8_t a, uint8_t b) { return (a != b) ? 1 : 0; }

int txn_transition_allowed(const TxnRecordV1 *cur, const TxnRecordV1 *next,
                           uint8_t writer, uint8_t physical_bank)
{
    uint8_t cs = cur->state;
    uint8_t ns = next->state;

    /* Sequence must strictly advance by one for every committed transition. */
    if (next->sequence != (uint32_t)(cur->sequence + 1u)) {
        return 0;
    }
    /* writer field in the new record must equal the acting writer. */
    if (next->writer != writer) {
        return 0;
    }
    /* Invariants must hold on the proposed record. */
    if (txn_check_invariants(next) != TXN_OK) {
        return 0;
    }

    /* Field inheritance: max_attempts is frozen once a candidate exists; it may
     * only be (re)set on CONFIRMED->PREPARED. layout_id never changes except at
     * RECOVERY. confirmed_* identity is immutable while a candidate is live. */
    if (ns != TXN_ST_RECOVERY) {
        if (next->layout_id != cur->layout_id) {
            return 0;
        }
    }

    /* Any writer may drive any state to RECOVERY when it cannot prove
     * consistency, but per 15.3 only PBL commits RECOVERY during boot. */
    if (ns == TXN_ST_RECOVERY) {
        return (writer == TXN_WR_PBL) ? 1 : 0;
    }

    switch (cs) {
    case TXN_ST_CONFIRMED:
        /* CONFIRMED -> PREPARED by C0: target = other physical bank, new gen. */
        if ((ns == TXN_ST_PREPARED) && (writer == TXN_WR_C0)) {
            if (!bank_is_other(next->target_bank, physical_bank)) {
                return 0;
            }
            if (next->txn_generation != (uint32_t)(cur->txn_generation + 1u)) {
                return 0;
            }
            if (next->confirmed_bank != physical_bank) {
                return 0;
            }
            return 1;
        }
        return 0;

    case TXN_ST_PREPARED:
        if (writer == TXN_WR_C0) {
            /* swap candidate (new generation) or cancel back to CONFIRMED. */
            if (ns == TXN_ST_PREPARED) {
                return (next->txn_generation != cur->txn_generation) ? 1 : 0;
            }
            if (ns == TXN_ST_CONFIRMED) {
                return (next->result == TXN_RES_CANCELLED) ? 1 : 0;
            }
            if (ns == TXN_ST_SWAP_REQUESTED) {
                /* generation inherited; banks inherited. */
                if ((next->txn_generation != cur->txn_generation) ||
                    (next->target_bank != cur->target_bank) ||
                    (next->confirmed_bank != cur->confirmed_bank)) {
                    return 0;
                }
                return 1;
            }
        }
        if ((writer == TXN_WR_PBL) && (ns == TXN_ST_ROLLBACK_REQUESTED)) {
            /* physical bank already target but swap not registered: result 7. */
            if (physical_bank != cur->target_bank) {
                return 0;
            }
            return (next->result == TXN_RES_LAYOUT_MISMATCH) ? 1 : 0;
        }
        return 0;

    case TXN_ST_SWAP_REQUESTED:
        if (writer == TXN_WR_C0) {
            /* physical bank still confirmed side: back to PREPARED/CONFIRMED. */
            if (physical_bank != cur->confirmed_bank) {
                return 0;
            }
            if ((ns == TXN_ST_PREPARED) || (ns == TXN_ST_CONFIRMED)) {
                return ((next->result == TXN_RES_SWAP_NOT_TAKEN) ||
                        (next->result == TXN_RES_HSE_ERROR)) ? 1 : 0;
            }
            return 0;
        }
        if (writer == TXN_WR_PBL) {
            if (physical_bank != cur->target_bank) {
                return 0;
            }
            if (ns == TXN_ST_TRIAL) {
                /* first trial: single write, attempts == 1. */
                return (next->attempts == 1u) ? 1 : 0;
            }
            if (ns == TXN_ST_ROLLBACK_REQUESTED) {
                return (next->result == TXN_RES_AUTH_FAIL) ? 1 : 0;
            }
        }
        return 0;

    case TXN_ST_TRIAL:
        if (writer == TXN_WR_PBL) {
            if (ns == TXN_ST_TRIAL) {
                /* attempts+1 on target side, below max. */
                if (physical_bank != cur->target_bank) {
                    return 0;
                }
                if (next->attempts != (uint8_t)(cur->attempts + 1u)) {
                    return 0;
                }
                return (next->attempts <= next->max_attempts) ? 1 : 0;
            }
            if (ns == TXN_ST_CONFIRMED) {
                /* confirm old package on confirmed side (result 1). */
                if (physical_bank != cur->confirmed_bank) {
                    return 0;
                }
                return 1;
            }
            if (ns == TXN_ST_ROLLBACK_REQUESTED) {
                return ((next->result == TXN_RES_TRIAL_LIMIT) ||
                        (next->result == TXN_RES_AUTH_FAIL)) ? 1 : 0;
            }
        }
        if (writer == TXN_WR_C0) {
            if (ns == TXN_ST_CONFIRMED) {
                /* confirm new package: token must match the live record. */
                if ((next->confirmed_bank != cur->target_bank) ||
                    (!digests_equal(next->confirmed_digest, cur->target_digest)) ||
                    (next->txn_generation != cur->txn_generation)) {
                    return 0;
                }
                return 1;
            }
            if (ns == TXN_ST_ROLLBACK_REQUESTED) {
                return ((next->result == TXN_RES_HEALTH_FAIL) ||
                        (next->result == TXN_RES_DEADLINE)) ? 1 : 0;
            }
        }
        return 0;

    case TXN_ST_ROLLBACK_REQUESTED:
        if (writer == TXN_WR_PBL) {
            if (ns == TXN_ST_CONFIRMED) {
                /* confirmed side, old package. */
                if (physical_bank != cur->confirmed_bank) {
                    return 0;
                }
                return 1;
            }
            if (ns == TXN_ST_ROLLBACK_REQUESTED) {
                /* still on target: bump retry count in flags (cap 2). */
                if (physical_bank != cur->target_bank) {
                    return 0;
                }
                if ((uint8_t)(next->flags & 0x0Fu) !=
                    (uint8_t)((cur->flags & 0x0Fu) + 1u)) {
                    return 0;
                }
                return ((uint8_t)(next->flags & 0x0Fu) <= 2u) ? 1 : 0;
            }
        }
        return 0;

    default:
        return 0;
    }
}
