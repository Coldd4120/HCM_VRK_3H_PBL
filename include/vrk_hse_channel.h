/* HSE MU channel liveness latch (LH-R1-005). Portable C99, no target headers.
 *
 * Problem: a bounded HSE poll that times out must not (a) be retried on the same
 * channel as if nothing happened, nor (b) let the request/response buffers be
 * reused while a late HSE write may still land in them. Both the PBL target path
 * (pbl_hw_s32k338.c) and the main adapter (vrk_hse_if.c) share this fail-closed
 * state machine:
 *   READY  --begin()-->  BUSY  --complete()-->  READY
 *   any    --fail()--->   FAILED   (latched; begin() refused until reset())
 * begin() is refused while BUSY (no buffer reuse) and while FAILED. Only an
 * explicit reliable reset (reset(), modelling power-on / functional reset)
 * leaves FAILED. */
#ifndef VRK_HSE_CHANNEL_H
#define VRK_HSE_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VRK_HSE_CH_READY = 0,
    VRK_HSE_CH_BUSY,
    VRK_HSE_CH_FAILED
} VrkHseChannelState;

typedef struct {
    VrkHseChannelState state;
} VrkHseChannel;

void vrk_hse_ch_init(VrkHseChannel *c);
/* Claim the channel for one request. Returns 1 on READY->BUSY, else 0 (BUSY or
 * FAILED): the caller must NOT touch the shared buffers when this returns 0. */
int  vrk_hse_ch_begin(VrkHseChannel *c);
/* Response consumed successfully: BUSY->READY. No-op unless BUSY. */
void vrk_hse_ch_complete(VrkHseChannel *c);
/* Timeout or protocol error: latch FAILED from any state. */
void vrk_hse_ch_fail(VrkHseChannel *c);
/* 1 only when a new request may start (READY). */
int  vrk_hse_ch_available(const VrkHseChannel *c);
/* Reliable reset (power-on / functional reset) clears FAILED back to READY. */
void vrk_hse_ch_reset(VrkHseChannel *c);

#ifdef __cplusplus
}
#endif
#endif /* VRK_HSE_CHANNEL_H */
