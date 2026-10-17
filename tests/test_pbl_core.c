#include "pbl_core.h"
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

typedef struct {
    PblRecord copies[2];
    uint32_t bank, verified_package, jumped_vector;
    int writes, jumps, swaps, resets, recoveries, auth_ok, write_ok;
} Fixture;

static bool bank(void *ctx, uint32_t *out)
{
    *out = ((Fixture *)ctx)->bank;
    return true;
}
static bool read_copy(void *ctx, uint32_t copy, PblRecord *out)
{
    *out = ((Fixture *)ctx)->copies[copy];
    return true;
}
static bool append(void *ctx, const PblRecord *record)
{
    Fixture *f = (Fixture *)ctx;
    if (!f->write_ok) return false;
    f->copies[f->writes++ & 1] = *record;
    return true;
}
static bool verify(void *ctx, uint32_t package, uint32_t *vector)
{
    Fixture *f = (Fixture *)ctx;
    f->verified_package = package;
    *vector = 0x00440000u;
    return f->auth_ok != 0;
}
static bool swap(void *ctx) { ((Fixture *)ctx)->swaps++; return true; }
static void reset(void *ctx) { ((Fixture *)ctx)->resets++; }
static void jump(void *ctx, uint32_t vector)
{
    Fixture *f = (Fixture *)ctx;
    f->jumps++;
    f->jumped_vector = vector;
}
static void recovery(void *ctx) { ((Fixture *)ctx)->recoveries++; }

static PblPort port(Fixture *f)
{
    PblPort p = {f, bank, read_copy, append, verify, swap, jump, reset, recovery};
    return p;
}
static Fixture fixture(uint32_t state, uint32_t current_bank)
{
    Fixture f = {0};
    PblRecord *r = &f.copies[0];
    f.bank = current_bank;
    f.auth_ok = f.write_ok = 1;
    r->magic = PBL_RECORD_MAGIC;
    r->version = PBL_RECORD_VERSION;
    r->sequence = 10u;
    r->state = state;
    r->confirmed_bank = 0u;
    r->target_bank = state == PBL_STATE_CONFIRMED ? 0u : 1u;
    r->confirmed_package_id = 0xA0u;
    r->target_package_id = state == PBL_STATE_CONFIRMED ? 0xA0u : 0xB0u;
    pbl_record_seal(r);
    return f;
}

int main(void)
{
    Fixture f;
    PblPort p;
    PblRecord selected;

    CHECK(pbl_crc32("123456789", 9u) == 0xCBF43926u);
    f = fixture(PBL_STATE_CONFIRMED, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.jumps == 1 && f.verified_package == 0xA0u && f.writes == 0);

    f = fixture(PBL_STATE_ACTIVATING, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xA0u && f.writes == 0);

    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xB0u && f.writes == 2);
    CHECK(f.copies[1].state == PBL_STATE_TRIAL &&
           f.copies[1].trial_boots == 1u);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.copies[0].trial_boots = PBL_MAX_TRIAL_BOOTS;
    pbl_record_seal(&f.copies[0]);
    CHECK(pbl_run(&p) == PBL_RESULT_RESET);
    CHECK(f.swaps == 1 && f.resets == 1 && f.writes == 1);
    CHECK(f.copies[0].state == PBL_STATE_ROLLING_BACK);

    f = fixture(PBL_STATE_ROLLING_BACK, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xA0u &&
           f.copies[0].state == PBL_STATE_CONFIRMED);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.auth_ok = 0;
    CHECK(pbl_run(&p) == PBL_RESULT_RESET && f.swaps == 1);

    f = fixture(PBL_STATE_CONFIRMED, 0u); p = port(&f);
    f.copies[0].crc32 ^= 1u;
    CHECK(pbl_run(&p) == PBL_RESULT_RECOVERY && f.recoveries == 1);

    f = fixture(PBL_STATE_CONFIRMED, 0u); p = port(&f);
    f.copies[1] = f.copies[0];
    f.copies[1].sequence++;
    f.copies[1].confirmed_package_id = 0xC0u;
    f.copies[1].target_package_id = 0xC0u;
    pbl_record_seal(&f.copies[1]);
    CHECK(pbl_select_record(&f.copies[0], &f.copies[1], &selected));
    CHECK(selected.confirmed_package_id == 0xC0u);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.write_ok = 0;
    CHECK(pbl_run(&p) == PBL_RESULT_RECOVERY && f.jumps == 0);
    puts("PBL host state-machine tests: PASS");
    return 0;
}

