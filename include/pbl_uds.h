#ifndef PBL_UDS_H
#define PBL_UDS_H

/* Recovery-mode UDS subset (Q09, design Sec.5.4). Host tested; no hardware.
 *
 *   0x10 DiagnosticSessionControl  01 default / 02 programming / 03 extended
 *   0x3E TesterPresent             00 (suppressPosRsp supported)
 *   0x22 ReadDataByIdentifier      one DID per request (single-frame ISO-TP)
 *   0x11 ECUReset                  01 hard / 03 soft -> reset after response
 *   0x34/0x36/0x37/0x31            NRC 0x22: download stays disabled until the
 *                                  OEM ODX, authentication (0x27/0x29) and the
 *                                  flash whitelist are approved (TODO-OEM)
 *   other SIDs                     NRC 0x11 (silent when functional)
 *
 * The DIDs below 0xFD00..0xFD02 are candidates in the system-supplier range
 * and must be confirmed against the OEM ODX before production.
 */

#include <stdbool.h>
#include <stdint.h>

#define PBL_UDS_SESSION_DEFAULT     0x01u
#define PBL_UDS_SESSION_PROGRAMMING 0x02u
#define PBL_UDS_SESSION_EXTENDED    0x03u
#define PBL_UDS_S3_MS               5000u   /* ISO 14229-2 S3server */
#define PBL_UDS_P2_MS               50u     /* ISO 14229-2 default P2server */
#define PBL_UDS_P2STAR_10MS         500u    /* 5000 ms in 10 ms units */

#define PBL_UDS_DID_ACTIVE_SESSION  0xF186u
#define PBL_UDS_DID_PBL_STATUS      0xFD00u /* reason, bank, record state */
#define PBL_UDS_DID_LAYOUT_ID       0xFD01u
#define PBL_UDS_DID_PBL_VERSION     0xFD02u

#define PBL_UDS_NRC_SNS   0x11u
#define PBL_UDS_NRC_SFNS  0x12u
#define PBL_UDS_NRC_IMLOIF 0x13u
#define PBL_UDS_NRC_CNC   0x22u
#define PBL_UDS_NRC_ROOR  0x31u

/* ISO-TP single frame on classic CAN (8 data bytes). */
#define PBL_ISOTP_SF_MAX  7u

typedef struct {
    uint32_t layout_id;
    uint32_t pbl_version;
    uint8_t recovery_reason;
    uint8_t physical_bank;  /* 0/1, 0xFF when unknown */
    uint8_t record_state;   /* PblState, 0 when no valid record */
} PblUdsInfo;

typedef struct {
    const PblUdsInfo *info;
    uint32_t last_request_ms;
    uint8_t session;
    bool reset_requested;
    uint8_t reset_type;
} PblUds;

void pbl_uds_init(PblUds *uds, const PblUdsInfo *info, uint32_t now_ms);
/* Returns the response length (0 = no response). rsp must hold >= 8 bytes. */
uint32_t pbl_uds_process(PblUds *uds, const uint8_t *req, uint32_t len,
                         bool functional, uint32_t now_ms,
                         uint8_t *rsp, uint32_t cap);
/* Falls back to the default session after S3 without a request. */
void pbl_uds_tick(PblUds *uds, uint32_t now_ms);

/* Single-frame ISO-TP. decode returns the payload length (1..7) or 0. */
uint32_t pbl_isotp_sf_decode(const uint8_t *frame, uint32_t dlc,
                             const uint8_t **payload);
bool pbl_isotp_sf_encode(const uint8_t *payload, uint32_t len,
                         uint8_t *frame, uint8_t pad);

#endif
