#include "txn_pbl.h"
#include <string.h>

/* Build next = cur with a new sequence and writer=PBL, then let the caller set
 * the changed fields. Returns next by value. */
static TxnRecordV1 base_next(const TxnRecordV1 *cur)
{
    TxnRecordV1 n = *cur;
    n.sequence = cur->sequence + 1u;
    n.writer = TXN_WR_PBL;
    n.result = TXN_RES_NONE;
    return n;
}

/* Commit helper: verify legality then call the port. Returns 1 on success. */
static int do_commit(const TxnRecordV1 *cur, const TxnRecordV1 *next,
                     uint8_t physical_bank, const TxnPblPorts *p)
{
    if (!txn_transition_allowed(cur, next, TXN_WR_PBL, physical_bank)) {
        return 0;
    }
    return (p->commit(p->ctx, next) != 0) ? 1 : 0;
}

TxnPblOutcome txn_pbl_run(const TxnRecordV1 *cur, uint8_t physical_bank,
                          const TxnPblPorts *ports)
{
    uint8_t B = physical_bank;
    int on_confirmed = (B == cur->confirmed_bank) ? 1 : 0;
    TxnRecordV1 n;

    switch (cur->state) {
    case TXN_ST_CONFIRMED:
        if (on_confirmed) {
            if (ports->auth(ports->ctx, cur->confirmed_digest)) {
                return TXN_PBL_JUMP_CONFIRMED;
            }
            return TXN_PBL_RECOVERY;
        }
        return TXN_PBL_RECOVERY; /* unregistered swap */

    case TXN_ST_PREPARED:
        if (on_confirmed) {
            return TXN_PBL_JUMP_CONFIRMED; /* boot old package, no write */
        }
        /* on target: protocol violation -> ROLLBACK_REQUESTED (result 7) */
        n = base_next(cur);
        n.state = TXN_ST_ROLLBACK_REQUESTED;
        n.result = TXN_RES_LAYOUT_MISMATCH;
        if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
        return TXN_PBL_ACTIVATE_RESET;

    case TXN_ST_SWAP_REQUESTED:
        if (on_confirmed) {
            return TXN_PBL_JUMP_CONFIRMED; /* boot old; C0 will resolve */
        }
        /* on target: authenticate target, then TRIAL a=1 (single write) */
        if (ports->auth(ports->ctx, cur->target_digest)) {
            n = base_next(cur);
            n.state = TXN_ST_TRIAL;
            n.attempts = 1u;
            if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
            return TXN_PBL_JUMP_TARGET;
        }
        n = base_next(cur);
        n.state = TXN_ST_ROLLBACK_REQUESTED;
        n.result = TXN_RES_AUTH_FAIL;
        if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
        return TXN_PBL_ACTIVATE_RESET;

    case TXN_ST_TRIAL:
        if (on_confirmed) {
            /* confirm old package on confirmed side (result 1) */
            if (!ports->auth(ports->ctx, cur->confirmed_digest)) {
                return TXN_PBL_RECOVERY;
            }
            n = base_next(cur);
            n.state = TXN_ST_CONFIRMED;
            n.result = TXN_RES_SWAP_NOT_TAKEN;
            n.attempts = 0u;
            n.flags = 0u;
            n.target_bank = cur->confirmed_bank;
            n.target_package_id = cur->confirmed_package_id;
            n.target_security_version = cur->confirmed_security_version;
            memcpy(n.target_digest, cur->confirmed_digest, TXN_DIGEST_LEN);
            if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
            return TXN_PBL_JUMP_CONFIRMED;
        }
        /* on target: a>=max or auth fail -> ROLLBACK_REQUESTED; else a+1 */
        if ((cur->attempts >= cur->max_attempts) ||
            (!ports->auth(ports->ctx, cur->target_digest))) {
            n = base_next(cur);
            n.state = TXN_ST_ROLLBACK_REQUESTED;
            n.result = (cur->attempts >= cur->max_attempts)
                       ? TXN_RES_TRIAL_LIMIT : TXN_RES_AUTH_FAIL;
            if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
            return TXN_PBL_ACTIVATE_RESET;
        }
        n = base_next(cur);
        n.state = TXN_ST_TRIAL;
        n.attempts = (uint8_t)(cur->attempts + 1u);
        if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
        return TXN_PBL_JUMP_TARGET;

    case TXN_ST_ROLLBACK_REQUESTED:
        if (on_confirmed) {
            if (!ports->auth(ports->ctx, cur->confirmed_digest)) {
                return TXN_PBL_RECOVERY;
            }
            n = base_next(cur);
            n.state = TXN_ST_CONFIRMED;
            n.attempts = 0u;
            n.flags = 0u;
            n.target_bank = cur->confirmed_bank;
            n.target_package_id = cur->confirmed_package_id;
            n.target_security_version = cur->confirmed_security_version;
            memcpy(n.target_digest, cur->confirmed_digest, TXN_DIGEST_LEN);
            if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
            return TXN_PBL_JUMP_CONFIRMED;
        }
        /* on target: bump retry count (cap) then activate+reset, else RECOVERY */
        if ((uint8_t)(cur->flags & 0x0Fu) >= TXN_PBL_RETRY_CAP) {
            return TXN_PBL_RECOVERY;
        }
        n = base_next(cur);
        n.state = TXN_ST_ROLLBACK_REQUESTED;
        n.flags = (uint8_t)((cur->flags & 0xF0u) |
                            (((cur->flags & 0x0Fu) + 1u) & 0x0Fu));
        if (!do_commit(cur, &n, B, ports)) { return TXN_PBL_RECOVERY; }
        return TXN_PBL_ACTIVATE_RESET;

    case TXN_ST_RECOVERY:
    default:
        return TXN_PBL_RECOVERY;
    }
}
