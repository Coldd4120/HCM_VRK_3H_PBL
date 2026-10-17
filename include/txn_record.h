/* VRK unified FOTA transaction record TxnRecordV1 (design D3 / detailed-design
 * section 15). Portable C99, no target headers: compiled identically by the PBL,
 * by Core0 (C0) and by the host tests. The media port is implemented separately
 * by each consumer (see txn_store.h).
 *
 * This file is hardware-independent. Nothing here reads or writes flash. */
#ifndef VRK_TXN_RECORD_H
#define VRK_TXN_RECORD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TXN_RECORD_SIZE      128u
#define TXN_BODY_LEN         120u   /* [0,120) programmed first                 */
#define TXN_CRC_OFFSET       112u   /* record_crc32 covers [0,112)              */
#define TXN_COMMIT_OFFSET    120u   /* [120,128) programmed separately          */
#define TXN_DIGEST_LEN       32u

#define TXN_MAGIC            0x31585456u /* "VTX1" little-endian                 */
#define TXN_SCHEMA_V1        1u
#define TXN_LENGTH_V1        128u
#define TXN_RESERVED_FF      0xFFFFFFFFu
#define TXN_COMMIT_WORD      0x434F4D54u /* "TMOC"                               */
#define TXN_COMMIT_INV_WORD  0xBCB0B2ABu /* bitwise-not of commit               */

/* State encodings (pairwise Hamming distance >= 4, and >= 4 from 0x00/0xFF). */
#define TXN_ST_CONFIRMED          0xC3u
#define TXN_ST_PREPARED           0xA5u
#define TXN_ST_SWAP_REQUESTED     0x5Au
#define TXN_ST_TRIAL              0x96u
#define TXN_ST_ROLLBACK_REQUESTED 0x69u
#define TXN_ST_RECOVERY           0x3Cu

/* Writers. */
#define TXN_WR_FACTORY 1u
#define TXN_WR_PBL     2u
#define TXN_WR_C0      3u

/* result codes */
#define TXN_RES_NONE           0u
#define TXN_RES_SWAP_NOT_TAKEN 1u
#define TXN_RES_TRIAL_LIMIT    2u
#define TXN_RES_AUTH_FAIL      3u
#define TXN_RES_HEALTH_FAIL    4u
#define TXN_RES_DEADLINE       5u
#define TXN_RES_STORE_FAIL     6u
#define TXN_RES_LAYOUT_MISMATCH 7u
#define TXN_RES_RECORD_CORRUPT 8u
#define TXN_RES_CANCELLED      9u
#define TXN_RES_HSE_ERROR      10u

typedef struct {
    uint32_t magic;
    uint16_t schema;
    uint16_t length;
    uint32_t sequence;
    uint32_t txn_generation;
    uint8_t  state;
    uint8_t  writer;
    uint8_t  confirmed_bank;
    uint8_t  target_bank;
    uint8_t  attempts;
    uint8_t  max_attempts;
    uint8_t  result;
    uint8_t  flags;
    uint32_t confirmed_package_id;
    uint32_t target_package_id;
    uint32_t layout_id;
    uint32_t confirmed_security_version;
    uint32_t target_security_version;
    uint32_t reserved;
    uint8_t  confirmed_digest[TXN_DIGEST_LEN];
    uint8_t  target_digest[TXN_DIGEST_LEN];
} TxnRecordV1;

/* Decode diagnostics. */
typedef enum {
    TXN_OK = 0,
    TXN_ERR_MAGIC,
    TXN_ERR_SCHEMA,
    TXN_ERR_LENGTH,
    TXN_ERR_CRC,
    TXN_ERR_COMMIT,
    TXN_ERR_INVARIANT
} TxnStatus;

/* CRC-32/ISO-HDLC (reflected 0xEDB88320, init/xor 0xFFFFFFFF); identical to
 * pbl_crc32 and to Python binascii.crc32. */
uint32_t txn_crc32(const uint8_t *data, size_t len);

/* Serialize a record to its 128-byte on-media form, including record_crc32,
 * reserved_ff and the commit unit. */
void txn_encode(const TxnRecordV1 *rec, uint8_t out[TXN_RECORD_SIZE]);

/* Serialize only the [0,120) body with its CRC and reserved_ff (commit bytes
 * left at 0xFF). Models the first program step of the two-step write. */
void txn_encode_body(const TxnRecordV1 *rec, uint8_t out[TXN_RECORD_SIZE]);

/* Fill the 8-byte commit unit [120,128). Models the second program step. */
void txn_fill_commit(uint8_t out[TXN_RECORD_SIZE]);

/* Parse the fixed fields; does not validate. */
void txn_decode(const uint8_t in[TXN_RECORD_SIZE], TxnRecordV1 *rec);

/* Structural validity: magic/schema/length, CRC over [0,112), and the commit
 * pair. Does NOT check state invariants. */
TxnStatus txn_validate_struct(const uint8_t in[TXN_RECORD_SIZE]);

/* State invariants of section 15.2. */
TxnStatus txn_check_invariants(const TxnRecordV1 *rec);

/* Full validity used by the loader: structural + invariants. Returns 1 when the
 * 128 bytes are a valid, committed record. */
int txn_record_valid(const uint8_t in[TXN_RECORD_SIZE], TxnRecordV1 *out_opt);

/* RFC-1982-style serial comparison: returns >0 if a is newer than b, <0 if
 * older, 0 if equal, and TXN_SERIAL_AMBIGUOUS if the difference is exactly
 * 2^31 (undecidable -> treat as corrupt). */
#define TXN_SERIAL_AMBIGUOUS 2
int txn_serial_cmp(uint32_t a, uint32_t b);

/* Pure permission function of section 15.3. Returns 1 if the transition
 * cur -> next is allowed for this writer at this physical bank, else 0. */
int txn_transition_allowed(const TxnRecordV1 *cur, const TxnRecordV1 *next,
                           uint8_t writer, uint8_t physical_bank);

#ifdef __cplusplus
}
#endif
#endif /* VRK_TXN_RECORD_H */
