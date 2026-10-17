/* Host tests for TxnRecordV1: T1 encode/decode & golden vectors, T2 transition
 * permissions, T3 selection serial comparison. Compiled for the host only. */
#include "txn_record.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

/* SHA-256 digests used by the golden vectors (precomputed, see txn_ref.py). */
static const uint8_t DG_26100801[32] = {
    0x22,0xbf,0xe0,0xa7,0x13,0x8a,0xe3,0xd2,0x8c,0x3a,0x0b,0x7b,0x20,0xda,0x3d,0x04,
    0x7c,0x69,0x17,0xe5,0x55,0x8f,0x04,0xd9,0x32,0xb5,0xbf,0x20,0x01,0xbb,0xc2,0x69};
static const uint8_t DG_26101701[32] = {
    0x59,0x41,0x3e,0x2f,0x32,0xc1,0xe3,0x09,0x00,0x6a,0xb9,0x68,0xcd,0xd4,0xf5,0xd5,
    0x1a,0xcb,0x39,0xc4,0x62,0x96,0xce,0xf6,0xb4,0x83,0x7c,0x79,0x7f,0x09,0xd7,0x66};

static int hexparse(const char *h, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) { return -1; }
        out[i] = (uint8_t)v;
    }
    return 0;
}

static const char *V1_HEX =
 "56545831010080000100000001000000c30100000003000001081026010810260000000001000000"
 "010000000000000022bfe0a7138ae3d28c3a0b7b20da3d047c6917e5558f04d932b5bf2001bbc269"
 "22bfe0a7138ae3d28c3a0b7b20da3d047c6917e5558f04d932b5bf2001bbc269d50adc59ffffffff"
 "544d4f43abb2b0bc";
static const char *V2_HEX =
 "56545831010080000500000002000000960200010103000001081026011710260000000001000000"
 "010000000000000022bfe0a7138ae3d28c3a0b7b20da3d047c6917e5558f04d932b5bf2001bbc269"
 "59413e2f32c1e309006ab968cdd4f5d51acb39c46296cef6b4837c797f09d76680bab26cffffffff"
 "544d4f43abb2b0bc";

static void test_crc(void)
{
    CHECK(txn_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u, "crc32 check value");
}

static void test_golden(void)
{
    uint8_t want[128], got[128];
    TxnRecordV1 r;

    CHECK(hexparse(V1_HEX, want, 128) == 0, "V1 hex parse");
    memset(&r, 0, sizeof r);
    r.schema = TXN_SCHEMA_V1; r.length = TXN_LENGTH_V1;
    r.sequence = 1; r.txn_generation = 1;
    r.state = TXN_ST_CONFIRMED; r.writer = TXN_WR_FACTORY;
    r.confirmed_bank = 0; r.target_bank = 0; r.attempts = 0; r.max_attempts = 3;
    r.confirmed_package_id = 0x26100801u; r.target_package_id = 0x26100801u;
    r.confirmed_security_version = 1; r.target_security_version = 1;
    memcpy(r.confirmed_digest, DG_26100801, 32);
    memcpy(r.target_digest, DG_26100801, 32);
    txn_encode(&r, got);
    CHECK(memcmp(got, want, 128) == 0, "V1 byte-exact encode");
    CHECK(txn_record_valid(want, NULL) == 1, "V1 valid");

    CHECK(hexparse(V2_HEX, want, 128) == 0, "V2 hex parse");
    memset(&r, 0, sizeof r);
    r.schema = TXN_SCHEMA_V1; r.length = TXN_LENGTH_V1;
    r.sequence = 5; r.txn_generation = 2;
    r.state = TXN_ST_TRIAL; r.writer = TXN_WR_PBL;
    r.confirmed_bank = 0; r.target_bank = 1; r.attempts = 1; r.max_attempts = 3;
    r.confirmed_package_id = 0x26100801u; r.target_package_id = 0x26101701u;
    r.confirmed_security_version = 1; r.target_security_version = 1;
    memcpy(r.confirmed_digest, DG_26100801, 32);
    memcpy(r.target_digest, DG_26101701, 32);
    txn_encode(&r, got);
    CHECK(memcmp(got, want, 128) == 0, "V2 byte-exact encode");
    CHECK(txn_record_valid(want, NULL) == 1, "V2 valid");

    /* round-trip decode */
    {
        TxnRecordV1 d;
        txn_decode(want, &d);
        CHECK(d.sequence == 5 && d.state == TXN_ST_TRIAL && d.attempts == 1, "V2 decode fields");
    }
}

static void corrupt_field(const char *hex, size_t byte, uint8_t xorval, const char *msg)
{
    uint8_t b[128];
    hexparse(hex, b, 128);
    b[byte] ^= xorval;
    CHECK(txn_record_valid(b, NULL) == 0, msg);
}

static void test_reject(void)
{
    corrupt_field(V1_HEX, 0, 0x01, "reject bad magic");
    corrupt_field(V1_HEX, 4, 0x01, "reject bad schema");
    corrupt_field(V1_HEX, 6, 0x01, "reject bad length");
    corrupt_field(V1_HEX, 48, 0x01, "reject body mutation (CRC)");
    corrupt_field(V1_HEX, 120, 0x01, "reject bad commit");
    corrupt_field(V1_HEX, 124, 0x01, "reject bad commit_inv");
    /* body present but commit unprogrammed (all 0xFF) must be invalid */
    {
        uint8_t b[128]; TxnRecordV1 r;
        memset(&r, 0, sizeof r);
        r.schema = TXN_SCHEMA_V1; r.length = TXN_LENGTH_V1; r.sequence = 1;
        r.state = TXN_ST_CONFIRMED; r.writer = TXN_WR_FACTORY; r.max_attempts = 3;
        txn_encode_body(&r, b);
        CHECK(txn_record_valid(b, NULL) == 0, "reject uncommitted body");
        txn_fill_commit(b);
        CHECK(txn_record_valid(b, NULL) == 1, "accept after commit");
    }
    /* invariant: CONFIRMED with differing banks must be rejected */
    {
        uint8_t b[128]; TxnRecordV1 r;
        memset(&r, 0, sizeof r);
        r.schema = TXN_SCHEMA_V1; r.length = TXN_LENGTH_V1; r.sequence = 1;
        r.state = TXN_ST_CONFIRMED; r.writer = TXN_WR_FACTORY; r.max_attempts = 3;
        r.target_bank = 1;
        txn_encode(&r, b);
        CHECK(txn_record_valid(b, NULL) == 0, "reject CONFIRMED bank mismatch");
    }
}

static void test_serial(void)
{
    CHECK(txn_serial_cmp(5, 4) > 0, "5>4");
    CHECK(txn_serial_cmp(4, 5) < 0, "4<5");
    CHECK(txn_serial_cmp(0, 0xFFFFFFFFu) > 0, "wrap 0>0xFFFFFFFF");
    CHECK(txn_serial_cmp(7, 7) == 0, "equal");
    CHECK(txn_serial_cmp(0, 0x80000000u) == TXN_SERIAL_AMBIGUOUS, "2^31 ambiguous");
}

/* T2: a small, representative slice of the 15.3 permission table. */
static void mkrec(TxnRecordV1 *r, uint8_t state, uint8_t writer, uint32_t seq,
                  uint8_t cb, uint8_t tb, uint8_t att, uint8_t maxa)
{
    memset(r, 0, sizeof *r);
    r->schema = TXN_SCHEMA_V1; r->length = TXN_LENGTH_V1;
    r->sequence = seq; r->state = state; r->writer = writer;
    r->confirmed_bank = cb; r->target_bank = tb; r->attempts = att; r->max_attempts = maxa;
    if (tb != cb) { r->target_digest[0] = 0xAAu; } /* make digests differ */
}

static void test_transitions(void)
{
    TxnRecordV1 cur, nxt;

    /* CONFIRMED -> PREPARED by C0 (allowed). */
    mkrec(&cur, TXN_ST_CONFIRMED, TXN_WR_FACTORY, 1, 0, 0, 0, 3);
    mkrec(&nxt, TXN_ST_PREPARED, TXN_WR_C0, 2, 0, 1, 0, 3);
    nxt.txn_generation = cur.txn_generation + 1u;
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_C0, 0) == 1, "CONFIRMED->PREPARED C0");
    /* same transition by PBL is forbidden */
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 0) == 0, "CONFIRMED->PREPARED PBL denied");

    /* SWAP_REQUESTED -> TRIAL a=1 by PBL on target side (allowed). */
    mkrec(&cur, TXN_ST_SWAP_REQUESTED, TXN_WR_C0, 3, 0, 1, 0, 3);
    mkrec(&nxt, TXN_ST_TRIAL, TXN_WR_PBL, 4, 0, 1, 1, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 1, "SWAP->TRIAL a=1 PBL");
    /* attempts=2 on first trial is forbidden */
    mkrec(&nxt, TXN_ST_TRIAL, TXN_WR_PBL, 4, 0, 1, 2, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 0, "SWAP->TRIAL a=2 denied");

    /* TRIAL attempts increment by PBL on target. */
    mkrec(&cur, TXN_ST_TRIAL, TXN_WR_PBL, 5, 0, 1, 1, 3);
    mkrec(&nxt, TXN_ST_TRIAL, TXN_WR_PBL, 6, 0, 1, 2, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 1, "TRIAL a++ PBL");
    /* skipping a count denied */
    mkrec(&nxt, TXN_ST_TRIAL, TXN_WR_PBL, 6, 0, 1, 3, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 0, "TRIAL a+=2 denied");

    /* sequence must be +1 */
    mkrec(&cur, TXN_ST_TRIAL, TXN_WR_PBL, 5, 0, 1, 1, 3);
    mkrec(&nxt, TXN_ST_TRIAL, TXN_WR_PBL, 7, 0, 1, 2, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 0, "sequence gap denied");

    /* any -> RECOVERY only by PBL */
    mkrec(&cur, TXN_ST_TRIAL, TXN_WR_PBL, 5, 0, 1, 1, 3);
    mkrec(&nxt, TXN_ST_RECOVERY, TXN_WR_PBL, 6, 0, 1, 1, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_PBL, 1) == 1, "any->RECOVERY PBL");
    mkrec(&nxt, TXN_ST_RECOVERY, TXN_WR_C0, 6, 0, 1, 1, 3);
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_C0, 1) == 0, "any->RECOVERY C0 denied");

    /* TRIAL -> CONFIRMED(new) by C0 with matching token. */
    mkrec(&cur, TXN_ST_TRIAL, TXN_WR_PBL, 8, 0, 1, 1, 3);
    cur.txn_generation = 2; cur.target_digest[0] = 0xAAu;
    mkrec(&nxt, TXN_ST_CONFIRMED, TXN_WR_C0, 9, 1, 1, 0, 3);
    nxt.txn_generation = 2; /* confirmed==target now; digests equal */
    nxt.confirmed_digest[0] = 0xAAu; nxt.target_digest[0] = 0xAAu;
    CHECK(txn_transition_allowed(&cur, &nxt, TXN_WR_C0, 1) == 1, "TRIAL->CONFIRMED(new) C0");
}

int main(void)
{
    test_crc();
    test_golden();
    test_reject();
    test_serial();
    test_transitions();
    if (failures == 0) {
        printf("PASS txn_record: golden vectors, reject cases, serial, transitions\n");
        return 0;
    }
    printf("txn_record FAILED: %d\n", failures);
    return 1;
}
