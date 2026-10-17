#include "pbl_core.h"

/* Deliberately fail closed until HSE bank/SMR, D-Flash journal, CAN recovery,
 * flash driver, and watchdog ports are implemented and validated. A production
 * build must replace this file and pass -DPBL_PRODUCTION. */
#ifdef PBL_PRODUCTION
#error "Unbound PBL board port must not enter a production image"
#endif

static bool no_bank(void *ctx, uint32_t *bank)
{
    (void)ctx; (void)bank;
    return false;
}
static bool no_record(void *ctx, uint32_t copy, PblRecord *record)
{
    (void)ctx; (void)copy; (void)record;
    return false;
}
static void stay_safe(void *ctx)
{
    (void)ctx;
}

const PblPort pbl_board_port = {
    .ctx = 0,
    .physical_bank = no_bank,
    .read_record = no_record,
    .append_record = 0,
    .verify_active_package = 0,
    .activate_passive = 0,
    .jump_c0 = 0,
    .reset = 0,
    .recovery = stay_safe
};
