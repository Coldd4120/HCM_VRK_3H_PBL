/* PBL boot decision over TxnRecordV1 (design section 15.6, "PBL" table). Pure
 * logic: given the loaded record, the real physical bank and the authentication
 * results, it decides what to commit and whether to jump, activate+reset, or
 * enter recovery. Any commit failure converges to RECOVERY. Portable C99. */
#ifndef VRK_TXN_PBL_H
#define VRK_TXN_PBL_H

#include "txn_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TXN_PBL_RETRY_CAP 2u   /* rollback-swap retry cap (flags bit0..3)       */

typedef enum {
    TXN_PBL_JUMP_CONFIRMED = 0, /* jump to the confirmed-side package           */
    TXN_PBL_JUMP_TARGET,        /* jump to the target-side (trial) package      */
    TXN_PBL_ACTIVATE_RESET,     /* activate the passive block and reset         */
    TXN_PBL_RECOVERY            /* enter recovery mode, do not jump             */
} TxnPblOutcome;

typedef struct {
    /* Authenticate the package described by digest. Returns 1 if valid. */
    int (*auth)(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]);
    /* Persist next (must already be a legal transition). Returns 1 on success. */
    int (*commit)(void *ctx, const TxnRecordV1 *next);
    void *ctx;
} TxnPblPorts;

/* Decide and (where required) commit. physical_bank is the HSE-reported bank. */
TxnPblOutcome txn_pbl_run(const TxnRecordV1 *cur, uint8_t physical_bank,
                          const TxnPblPorts *ports);

#ifdef __cplusplus
}
#endif
#endif /* VRK_TXN_PBL_H */
