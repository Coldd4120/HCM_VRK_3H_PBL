#include "vrk_hse_channel.h"

void vrk_hse_ch_init(VrkHseChannel *c) { c->state = VRK_HSE_CH_READY; }

int vrk_hse_ch_begin(VrkHseChannel *c)
{
    if (c->state != VRK_HSE_CH_READY) {
        return 0; /* BUSY: no buffer reuse; FAILED: channel dead until reset */
    }
    c->state = VRK_HSE_CH_BUSY;
    return 1;
}

void vrk_hse_ch_complete(VrkHseChannel *c)
{
    if (c->state == VRK_HSE_CH_BUSY) {
        c->state = VRK_HSE_CH_READY;
    }
    /* complete() on FAILED is ignored: a latched failure stays latched. */
}

void vrk_hse_ch_fail(VrkHseChannel *c) { c->state = VRK_HSE_CH_FAILED; }

int vrk_hse_ch_available(const VrkHseChannel *c)
{
    return (c->state == VRK_HSE_CH_READY) ? 1 : 0;
}

void vrk_hse_ch_reset(VrkHseChannel *c) { c->state = VRK_HSE_CH_READY; }
