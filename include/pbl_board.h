#ifndef PBL_BOARD_H
#define PBL_BOARD_H

#include "pbl_core.h"

/* Board binding selected at configure time (CMake PBL_BOARD). */
extern const PblPort pbl_board_port;
/* First call after the C runtime is up: safe outputs, watchdog. */
void pbl_board_init(void);

#ifdef PBL_TXN_BACKEND
/* LH-R1-002: board journal media + digest authenticator for pbl_run_txn. */
const TxnMedia *pbl_board_txn_media(void);
int pbl_board_txn_auth(void *ctx, const uint8_t digest[TXN_DIGEST_LEN]);
#endif

#endif
