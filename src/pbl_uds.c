#include "pbl_uds.h"
#include <stddef.h>

void pbl_uds_init(PblUds *u, const PblUdsInfo *info, uint32_t now_ms)
{
    if (u != NULL) {
        u->info = info;
        u->last_request_ms = now_ms;
        u->session = PBL_UDS_SESSION_DEFAULT;
        u->reset_requested = false;
        u->reset_type = 0u;
    }
}

void pbl_uds_tick(PblUds *u, uint32_t now_ms)
{
    if (u != NULL && u->session != PBL_UDS_SESSION_DEFAULT &&
        (uint32_t)(now_ms - u->last_request_ms) >= PBL_UDS_S3_MS) {
        u->session = PBL_UDS_SESSION_DEFAULT;
    }
}

static uint32_t nrc(uint8_t sid, uint8_t code, bool functional, uint8_t *rsp)
{
    /* ISO 14229-1: functional requests suppress SNS/SFNS/ROOR. */
    if (functional && (code == PBL_UDS_NRC_SNS || code == PBL_UDS_NRC_SFNS ||
                       code == PBL_UDS_NRC_ROOR)) {
        return 0u;
    }
    rsp[0] = 0x7Fu;
    rsp[1] = sid;
    rsp[2] = code;
    return 3u;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t read_did(const PblUds *u, uint16_t did, uint8_t *rsp)
{
    const PblUdsInfo *i = u->info;
    rsp[0] = 0x62u;
    rsp[1] = (uint8_t)(did >> 8);
    rsp[2] = (uint8_t)did;
    switch (did) {
    case PBL_UDS_DID_ACTIVE_SESSION:
        rsp[3] = u->session;
        return 4u;
    case PBL_UDS_DID_PBL_STATUS:
        if (i == NULL) return 0u;
        rsp[3] = i->recovery_reason;
        rsp[4] = i->physical_bank;
        rsp[5] = i->record_state;
        return 6u;
    case PBL_UDS_DID_LAYOUT_ID:
        if (i == NULL) return 0u;
        put32(&rsp[3], i->layout_id);
        return 7u;
    case PBL_UDS_DID_PBL_VERSION:
        if (i == NULL) return 0u;
        put32(&rsp[3], i->pbl_version);
        return 7u;
    default:
        return 0u;
    }
}

uint32_t pbl_uds_process(PblUds *u, const uint8_t *req, uint32_t len,
                         bool functional, uint32_t now_ms,
                         uint8_t *rsp, uint32_t cap)
{
    uint8_t sid, sub;
    bool suppress;
    uint32_t n;
    if (u == NULL || req == NULL || rsp == NULL || len == 0u ||
        cap < PBL_ISOTP_SF_MAX) {
        return 0u;
    }
    u->last_request_ms = now_ms;
    sid = req[0];
    sub = (uint8_t)((len > 1u) ? (req[1] & 0x7Fu) : 0u);
    suppress = (len > 1u) && ((req[1] & 0x80u) != 0u);
    switch (sid) {
    case 0x10u:
        if (len != 2u) return nrc(sid, PBL_UDS_NRC_IMLOIF, functional, rsp);
        if (sub != PBL_UDS_SESSION_DEFAULT && sub != PBL_UDS_SESSION_PROGRAMMING &&
            sub != PBL_UDS_SESSION_EXTENDED) {
            return nrc(sid, PBL_UDS_NRC_SFNS, functional, rsp);
        }
        u->session = sub;
        if (suppress) return 0u;
        rsp[0] = 0x50u;
        rsp[1] = sub;
        rsp[2] = 0x00u;
        rsp[3] = (uint8_t)PBL_UDS_P2_MS;
        rsp[4] = (uint8_t)(PBL_UDS_P2STAR_10MS >> 8);
        rsp[5] = (uint8_t)PBL_UDS_P2STAR_10MS;
        return 6u;
    case 0x3Eu:
        if (len != 2u) return nrc(sid, PBL_UDS_NRC_IMLOIF, functional, rsp);
        if (sub != 0x00u) return nrc(sid, PBL_UDS_NRC_SFNS, functional, rsp);
        if (suppress) return 0u;
        rsp[0] = 0x7Eu;
        rsp[1] = 0x00u;
        return 2u;
    case 0x22u:
        if (len != 3u) return nrc(sid, PBL_UDS_NRC_IMLOIF, functional, rsp);
        n = read_did(u, (uint16_t)(((uint16_t)req[1] << 8) | req[2]), rsp);
        return n != 0u ? n : nrc(sid, PBL_UDS_NRC_ROOR, functional, rsp);
    case 0x11u:
        if (len != 2u) return nrc(sid, PBL_UDS_NRC_IMLOIF, functional, rsp);
        if (sub != 0x01u && sub != 0x03u) {
            return nrc(sid, PBL_UDS_NRC_SFNS, functional, rsp);
        }
        u->reset_requested = true;
        u->reset_type = sub;
        if (suppress) return 0u;
        rsp[0] = 0x51u;
        rsp[1] = sub;
        return 2u;
    case 0x31u:
    case 0x34u:
    case 0x36u:
    case 0x37u:
        /* Download disabled in this stage (TODO-OEM: ODX, auth, whitelist). */
        return nrc(sid, PBL_UDS_NRC_CNC, functional, rsp);
    default:
        return nrc(sid, PBL_UDS_NRC_SNS, functional, rsp);
    }
}

uint32_t pbl_isotp_sf_decode(const uint8_t *frame, uint32_t dlc,
                             const uint8_t **payload)
{
    uint32_t n;
    if (frame == NULL || payload == NULL || dlc == 0u || dlc > 8u ||
        (frame[0] & 0xF0u) != 0x00u) {
        return 0u;
    }
    n = frame[0] & 0x0Fu;
    if (n == 0u || n > PBL_ISOTP_SF_MAX || n + 1u > dlc) {
        return 0u;
    }
    *payload = &frame[1];
    return n;
}

bool pbl_isotp_sf_encode(const uint8_t *payload, uint32_t len,
                         uint8_t *frame, uint8_t pad)
{
    uint32_t i;
    if (payload == NULL || frame == NULL || len == 0u || len > PBL_ISOTP_SF_MAX) {
        return false;
    }
    frame[0] = (uint8_t)len;
    for (i = 0u; i < 7u; ++i) {
        frame[1u + i] = (i < len) ? payload[i] : pad;
    }
    return true;
}
