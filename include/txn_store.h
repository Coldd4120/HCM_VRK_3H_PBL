/* TxnRecordV1 journal over two erase units (section 15.4/15.5). Portable C99.
 * The medium is abstracted by TxnMedia so the PBL, C0 and the host tests share
 * one implementation; only the port differs. Programming is NOR-like: a slot is
 * written in two steps (body then commit unit) and may be interrupted. */
#ifndef VRK_TXN_STORE_H
#define VRK_TXN_STORE_H

#include "txn_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TXN_SLOTS_PER_UNIT 64u
#define TXN_UNIT_COUNT      2u

/* Media port. All functions return 0 on success, non-zero on hardware error.
 * read: copy 128 bytes of (unit,slot) into out.
 * program: AND-program len bytes at (unit, slot, off) from src (clear-only).
 *          off/len align to the record's program unit; callers use [0,120) then
 *          [120,128).
 * erase: set the whole unit to 0xFF. */
typedef struct TxnMedia {
    int (*read)(void *ctx, uint32_t unit, uint32_t slot, uint8_t out[TXN_RECORD_SIZE]);
    int (*program)(void *ctx, uint32_t unit, uint32_t slot, uint32_t off,
                   const uint8_t *src, uint32_t len);
    int (*erase)(void *ctx, uint32_t unit);
    void *ctx;
    uint32_t slots_per_unit;
    uint32_t units;
} TxnMedia;

typedef enum {
    TXN_LOAD_OK = 0,        /* a valid latest record was found       */
    TXN_LOAD_EMPTY,         /* both units blank (Factory territory)  */
    TXN_LOAD_CORRUPT,       /* ambiguous / duplicate-max / none-valid-but-not-blank */
    TXN_LOAD_HW_ERROR
} TxnLoadStatus;

typedef struct {
    TxnRecordV1 record;
    uint32_t unit;
    uint32_t slot;
    int valid;              /* 1 if record/unit/slot are meaningful */
} TxnLocation;

/* Scan both units, pick the record with the greatest serial sequence. */
TxnLoadStatus txn_store_load(const TxnMedia *m, TxnLocation *out);

typedef enum {
    TXN_APPEND_OK = 0,
    TXN_APPEND_HW_ERROR,
    TXN_APPEND_VERIFY_FAIL,
    TXN_APPEND_FULL_ERROR
} TxnAppendStatus;

/* Append an already-constructed record (its sequence must be load()+1). Writes
 * body then commit with read-back verification. When the current unit is full,
 * writes into the other unit's first slot and erases the old unit afterwards
 * (unit switch, section 15.4). */
TxnAppendStatus txn_store_append(const TxnMedia *m, const TxnRecordV1 *rec);

#ifdef __cplusplus
}
#endif
#endif /* VRK_TXN_STORE_H */
