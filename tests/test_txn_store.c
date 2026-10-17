/* T3/T4: journal selection, unit switch, and power-cut semantics W1..W7 using a
 * simulated interruptible D-Flash (NOR AND-program, per-slot two-step write). */
#include "txn_store.h"
#include <stdio.h>
#include <string.h>

#define UNITS 2u
#define SLOTS 64u

typedef struct {
    uint8_t data[UNITS][SLOTS][TXN_RECORD_SIZE];
    int fail_after;   /* abort the Nth program sub-op (byte-run); <0 disables */
    int prog_count;
    int aborted;
} Sim;

static void sim_init(Sim *s)
{
    memset(s->data, 0xFF, sizeof s->data);
    s->fail_after = -1; s->prog_count = 0; s->aborted = 0;
}

static int sim_read(void *ctx, uint32_t u, uint32_t sl, uint8_t out[TXN_RECORD_SIZE])
{
    Sim *s = (Sim *)ctx;
    memcpy(out, s->data[u][sl], TXN_RECORD_SIZE);
    return 0;
}

static int sim_program(void *ctx, uint32_t u, uint32_t sl, uint32_t off,
                       const uint8_t *src, uint32_t len)
{
    Sim *s = (Sim *)ctx;
    uint32_t i;
    /* model word-by-word programmability with a mid-write power cut */
    for (i = 0u; i < len; i += 4u) {
        s->prog_count++;
        if (s->fail_after >= 0 && s->prog_count > s->fail_after) {
            s->aborted = 1;
            return -1; /* power lost mid-program */
        }
        {
            uint32_t j;
            for (j = 0u; j < 4u && (i + j) < len; j++) {
                /* NOR: clear-only (AND). */
                s->data[u][sl][off + i + j] &= src[i + j];
            }
        }
    }
    return 0;
}

static int sim_erase(void *ctx, uint32_t u)
{
    Sim *s = (Sim *)ctx;
    s->prog_count++;
    if (s->fail_after >= 0 && s->prog_count > s->fail_after) {
        /* partial erase: clear half the unit then fail */
        memset(s->data[u], 0x00, sizeof s->data[u] / 2);
        s->aborted = 1;
        return -1;
    }
    memset(s->data[u], 0xFF, sizeof s->data[u]);
    return 0;
}

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } } while (0)

static void mkrec(TxnRecordV1 *r, uint8_t state, uint8_t writer, uint32_t seq,
                  uint8_t cb, uint8_t tb, uint8_t att, uint8_t maxa)
{
    memset(r, 0, sizeof *r);
    r->schema = TXN_SCHEMA_V1; r->length = TXN_LENGTH_V1;
    r->sequence = seq; r->state = state; r->writer = writer;
    r->confirmed_bank = cb; r->target_bank = tb; r->attempts = att; r->max_attempts = maxa;
    if (tb != cb) { r->target_digest[0] = 0xAAu; }
}

static TxnMedia mkmedia(Sim *s)
{
    TxnMedia m;
    m.read = sim_read; m.program = sim_program; m.erase = sim_erase;
    m.ctx = s; m.slots_per_unit = SLOTS; m.units = UNITS;
    return m;
}

static void test_empty_and_append(void)
{
    Sim s; sim_init(&s);
    TxnMedia m = mkmedia(&s);
    TxnLocation loc;
    TxnRecordV1 r;

    CHECK(txn_store_load(&m, &loc) == TXN_LOAD_EMPTY, "blank -> EMPTY");
    mkrec(&r, TXN_ST_CONFIRMED, TXN_WR_FACTORY, 1, 0, 0, 0, 3);
    CHECK(txn_store_append(&m, &r) == TXN_APPEND_OK, "append first");
    CHECK(txn_store_load(&m, &loc) == TXN_LOAD_OK && loc.record.sequence == 1, "load seq1");
    CHECK(loc.unit == 0 && loc.slot == 0, "located u0 s0");

    mkrec(&r, TXN_ST_PREPARED, TXN_WR_C0, 2, 0, 1, 0, 3);
    CHECK(txn_store_append(&m, &r) == TXN_APPEND_OK, "append second");
    CHECK(txn_store_load(&m, &loc) == TXN_LOAD_OK && loc.record.sequence == 2, "load seq2");
    CHECK(loc.slot == 1, "second at slot1");
}

static int slot_blank_check(Sim *s, uint32_t u, uint32_t sl);
static void test_unit_switch(void)
{
    Sim s; sim_init(&s);
    TxnMedia m = mkmedia(&s);
    TxnLocation loc;
    TxnRecordV1 r;
    uint32_t i;

    /* fill unit 0 completely (64 records) */
    for (i = 1u; i <= SLOTS; i++) {
        mkrec(&r, TXN_ST_TRIAL, TXN_WR_PBL, i, 0, 1, 1, 3);
        CHECK(txn_store_append(&m, &r) == TXN_APPEND_OK, "fill unit0");
    }
    CHECK(txn_store_load(&m, &loc) == TXN_LOAD_OK && loc.record.sequence == SLOTS, "unit0 full max");
    CHECK(loc.unit == 0 && loc.slot == SLOTS - 1u, "last slot unit0");
    /* next append must switch to unit 1 slot 0 and erase unit 0 */
    mkrec(&r, TXN_ST_TRIAL, TXN_WR_PBL, SLOTS + 1u, 0, 1, 1, 3);
    CHECK(txn_store_append(&m, &r) == TXN_APPEND_OK, "unit switch append");
    CHECK(txn_store_load(&m, &loc) == TXN_LOAD_OK && loc.record.sequence == SLOTS + 1u, "after switch");
    CHECK(loc.unit == 1 && loc.slot == 0, "now in unit1 slot0");
    CHECK(slot_blank_check(&s, 0, 0), "unit0 erased");
}

static int slot_blank_check(Sim *s, uint32_t u, uint32_t sl)
{
    uint32_t i;
    for (i = 0; i < TXN_RECORD_SIZE; i++) {
        if (s->data[u][sl][i] != 0xFF) return 0;
    }
    return 1;
}

static void test_powercut(void)
{
    /* Seed a committed record, then try appending a new one while cutting power
     * at each program sub-step. After every cut, load() must return the OLD
     * record (never the half-written candidate) or OK with the new one only if
     * the commit unit fully landed. */
    int cut;
    for (cut = 0; cut < 40; cut++) {
        Sim s; sim_init(&s);
        TxnMedia m = mkmedia(&s);
        TxnRecordV1 r; TxnLocation loc;
        /* seed seq1 confirmed */
        mkrec(&r, TXN_ST_CONFIRMED, TXN_WR_FACTORY, 1, 0, 0, 0, 3);
        txn_store_append(&m, &r);
        /* now append seq2, cutting after `cut` program ops */
        s.prog_count = 0; s.fail_after = cut; s.aborted = 0;
        mkrec(&r, TXN_ST_PREPARED, TXN_WR_C0, 2, 0, 1, 0, 3);
        (void)txn_store_append(&m, &r);
        /* reload */
        s.fail_after = -1;
        TxnLoadStatus ls = txn_store_load(&m, &loc);
        CHECK(ls == TXN_LOAD_OK, "post-cut load ok");
        if (ls == TXN_LOAD_OK) {
            /* either still seq1 (candidate not committed) or seq2 (fully done) */
            CHECK(loc.record.sequence == 1 || loc.record.sequence == 2, "seq is 1 or 2");
            if (!s.aborted) {
                CHECK(loc.record.sequence == 2, "no cut -> seq2");
            }
            /* The invariant that matters: a PREPARED candidate must never be
             * loaded as CONFIRMED. If seq2 present it is PREPARED; if seq1 it is
             * CONFIRMED. Both are self-consistent. */
            if (loc.record.sequence == 2) {
                CHECK(loc.record.state == TXN_ST_PREPARED, "seq2 is PREPARED");
            } else {
                CHECK(loc.record.state == TXN_ST_CONFIRMED, "seq1 is CONFIRMED");
            }
        }
    }
}

int main(void)
{
    test_empty_and_append();
    test_unit_switch();
    test_powercut();
    if (failures == 0) {
        printf("PASS txn_store: empty/append, unit switch, W1..W7 power-cut\n");
        return 0;
    }
    printf("txn_store FAILED: %d\n", failures);
    return 1;
}
