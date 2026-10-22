#ifndef PBL_CORE_H
#define PBL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "gen/pbl_layout.h"

#define PBL_RECORD_MAGIC 0x50424C31u
#define PBL_RECORD_VERSION 1u
/* At most this many boots of a TRIAL package; the activation boot is boot 1
 * (design Sec.15.6). Single source with C0 VRK_FOTA_TRIAL_MAX_BOOTS:
 * layout.json boot_policy.max_trial_attempts (candidate value, to confirm). */
#define PBL_MAX_TRIAL_BOOTS (PBL_LAYOUT_TRIAL_MAX_ATTEMPTS)

/* C0 vector table: 16 Cortex-M7 system entries + 242 peripheral entries
 * (Vector_Table.s: 258 words = 0x408 bytes; Reset_Handler follows at +0x420).
 * NUMBER_OF_INT_VECTORS in the RTD S32K358_COMMON.h used for S32K338 is 249,
 * which is smaller than the table the C0 image actually links, so the gate
 * must use the real image length: with 249 the window check would accept a
 * reset handler at +0x3E4..+0x407, i.e. inside the real vector table. */
#define PBL_C0_VECTOR_WORDS (258u)
#define PBL_C0_VECTOR_BYTES (PBL_C0_VECTOR_WORDS * 4u)

#if (PBL_C0_VECTOR_BYTES > PBL_LAYOUT_C0_VECTOR_ALIGN)
#error "C0 vector table does not fit in the layout vector_align block (VTOR)"
#endif
#if ((PBL_LAYOUT_C0_VECTOR_ALIGN & (PBL_LAYOUT_C0_VECTOR_ALIGN - 1u)) != 0u) || \
    (PBL_LAYOUT_C0_VECTOR_ALIGN < 0x80u)
#error "layout vector_align must be a power of two >= 0x80 (ARMv7-M VTOR)"
#endif

typedef enum {
    PBL_STATE_CONFIRMED = 1u,
    PBL_STATE_ACTIVATING = 2u,
    PBL_STATE_TRIAL = 3u,
    PBL_STATE_ROLLING_BACK = 4u
} PblState;

/* Little endian persistent record. The storage port must append, read back,
 * and verify a complete record before returning success. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t state;
    uint32_t confirmed_bank;
    uint32_t target_bank;
    uint32_t trial_boots;
    uint32_t confirmed_package_id;
    uint32_t target_package_id;
    uint32_t crc32;
} PblRecord;

typedef struct {
    void *ctx;
    bool (*physical_bank)(void *ctx, uint32_t *bank);
    bool (*read_record)(void *ctx, uint32_t copy, PblRecord *record);
    bool (*append_record)(void *ctx, const PblRecord *record);
    bool (*verify_active_package)(void *ctx, uint32_t package_id,
                                  uint32_t *c0_vector);
    bool (*activate_passive)(void *ctx);
    void (*jump_c0)(void *ctx, uint32_t vector);
    void (*reset)(void *ctx);
    void (*recovery)(void *ctx);
    /* Reads one word of the authenticated active image (vector table). */
    bool (*read_u32)(void *ctx, uint32_t address, uint32_t *value);
} PblPort;

typedef enum {
    PBL_RESULT_JUMP = 1,
    PBL_RESULT_RESET = 2,
    PBL_RESULT_RECOVERY = 3
} PblResult;

uint32_t pbl_crc32(const void *data, uint32_t length);
void pbl_record_seal(PblRecord *record);
bool pbl_record_valid(const PblRecord *record);
bool pbl_select_record(const PblRecord *a, const PblRecord *b,
                       PblRecord *chosen);
/* Q09: true only for a vector table that VTOR may point at and that the
 * PBL may hand control to: inside the C0 region of the active half, aligned
 * to the layout vector_align, whole table inside the region, initial MSP in
 * the C0 MSP window (8-byte aligned) and a Thumb reset handler that lies in
 * the C0 region behind the table. */
bool pbl_c0_vector_ok(const PblPort *port, uint32_t vector);
PblResult pbl_run(const PblPort *port);

#ifdef PBL_TXN_BACKEND
#include "txn_store.h"
#include "txn_pbl.h"
/* LH-R1-003: optional TxnRecordV1 boot-decision path (design Sec.15.6), chosen
 * at compile time by PBL_TXN_BACKEND. The default build never defines it, so the
 * release image is byte-identical. The journal media and the digest authenticator
 * are supplied by the board back end (LH-R1-002); both fail closed until the
 * hardware parameters (D-Flash units, HSE SMR verify) are confirmed. */
PblResult pbl_run_txn(const PblPort *p, const TxnMedia *media,
                      int (*auth)(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]));
#endif

#endif
