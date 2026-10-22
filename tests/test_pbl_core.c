#include "pbl_core.h"
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

#define C0_BASE   PBL_LAYOUT_C0_BASE
#define C0_END    PBL_LAYOUT_C0_END
#define ALIGN     PBL_LAYOUT_C0_VECTOR_ALIGN
#define MSP_TOP   PBL_LAYOUT_RAM_C0_MSP_WINDOW_END
#define MSP_BASE  PBL_LAYOUT_RAM_C0_MSP_WINDOW_BASE

typedef struct {
    PblRecord copies[2];
    uint32_t bank, verified_package, jumped_vector;
    uint32_t vector, msp, entry;      /* image presented by the fake flash */
    int writes, jumps, swaps, resets, recoveries, auth_ok, write_ok, read_ok, reads;
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
    *vector = f->vector;
    return f->auth_ok != 0;
}
static bool read_u32(void *ctx, uint32_t address, uint32_t *value)
{
    Fixture *f = (Fixture *)ctx;
    f->reads++;
    if (!f->read_ok) return false;
    if (address == f->vector) { *value = f->msp; return true; }
    if (address == f->vector + 4u) { *value = f->entry; return true; }
    return false;
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
    PblPort p = {f, bank, read_copy, append, verify, swap, jump, reset, recovery, read_u32};
    return p;
}
static void image(Fixture *f, uint32_t vector, uint32_t msp, uint32_t entry)
{
    f->vector = vector;
    f->msp = msp;
    f->entry = entry;
}
static Fixture fixture(uint32_t state, uint32_t current_bank)
{
    Fixture f = {0};
    PblRecord *r = &f.copies[0];
    f.bank = current_bank;
    f.auth_ok = f.write_ok = f.read_ok = 1;
    image(&f, C0_BASE, MSP_TOP, (C0_BASE + 0x1000u) | 1u);
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
static void new_boot(Fixture *f)
{
    f->jumps = f->swaps = f->resets = f->recoveries = 0;
    f->jumped_vector = 0u;
}

/* Q09 boundary / alignment negatives on the vector gate itself. */
static int vector_gate(void)
{
    Fixture f = fixture(PBL_STATE_CONFIRMED, 0u);
    PblPort p = port(&f);
    static const uint32_t bad_vectors[] = {
        0x00440000u,                 /* old window start = descriptor */
        C0_BASE - ALIGN,             /* aligned, inside the descriptor */
        C0_BASE + 0x80u,             /* 128-byte aligned only */
        C0_BASE + 0x200u,            /* old 0x200 alignment */
        C0_BASE + (ALIGN / 2u),      /* half of vector_align */
        C0_BASE + 4u,
        C0_END,                      /* first byte after C0 */
        PBL_LAYOUT_C1_BASE,          /* C1 vector table */
        PBL_LAYOUT_PBL_VECTOR,       /* the PBL itself */
        PBL_LAYOUT_C0_PASSIVE_BASE,  /* passive copy, never executable */
        PBL_LAYOUT_HSE_TAIL_BASE,
        0xFFFFF800u, 0u
    };
    uint32_t i;
    CHECK(pbl_c0_vector_ok(&p, C0_BASE));
    for (i = 0u; i < sizeof(bad_vectors) / sizeof(bad_vectors[0]); ++i) {
        image(&f, bad_vectors[i], MSP_TOP, (bad_vectors[i] + 0x800u) | 1u);
        f.reads = 0;
        if (pbl_c0_vector_ok(&p, bad_vectors[i])) {
            printf("FAIL vector 0x%08lX accepted\n", (unsigned long)bad_vectors[i]);
            return 1;
        }
        /* A rejected window/alignment must not even be read: the address may
         * be unmapped (bus fault) or belong to another region. */
        if (f.reads != 0) {
            printf("FAIL vector 0x%08lX was read before the window check\n",
                   (unsigned long)bad_vectors[i]);
            return 1;
        }
    }
    /* Last aligned slot that still holds the table plus code: accepted. */
    image(&f, C0_END - ALIGN, MSP_TOP, (C0_END - ALIGN + PBL_C0_VECTOR_BYTES) | 1u);
    CHECK(pbl_c0_vector_ok(&p, C0_END - ALIGN));
    /* ... but the reset handler must not point past the region. */
    image(&f, C0_END - ALIGN, MSP_TOP, C0_END | 1u);
    CHECK(!pbl_c0_vector_ok(&p, C0_END - ALIGN));

    /* Initial MSP: (base, top], 8-byte aligned. */
    image(&f, C0_BASE, MSP_TOP, (C0_BASE + 0x1000u) | 1u);
    CHECK(pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = MSP_BASE;            CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = MSP_TOP + 8u;        CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = MSP_TOP - 4u;        CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = 0x20400000u;         CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = 0xFFFFFFF8u;         CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = MSP_BASE + 8u;       CHECK(pbl_c0_vector_ok(&p, C0_BASE));
    f.msp = MSP_TOP;

    /* Reset handler: Thumb, behind the table, inside C0. */
    f.entry = C0_BASE + 0x1000u;                    CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = (C0_BASE + 4u) | 1u;                  CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = (C0_BASE + PBL_C0_VECTOR_BYTES - 2u) | 1u; CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = (C0_BASE + PBL_C0_VECTOR_BYTES) | 1u; CHECK(pbl_c0_vector_ok(&p, C0_BASE));
    /* Regression: the real C0 build (Vector_Table.s ends at +0x408) has
     * Reset_Handler at +0x420 and must be accepted. */
    f.entry = (C0_BASE + 0x420u) | 1u;              CHECK(pbl_c0_vector_ok(&p, C0_BASE));
    CHECK(PBL_C0_VECTOR_BYTES == 258u * 4u);
    f.entry = C0_END | 1u;                          CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = PBL_LAYOUT_C1_BASE | 1u;              CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = PBL_LAYOUT_PBL_VECTOR | 1u;           CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = 0x00000201u;                          CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.entry = (C0_BASE + 0x1000u) | 1u;

    f.read_ok = 0;               CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    f.read_ok = 1;
    p.read_u32 = NULL;           CHECK(!pbl_c0_vector_ok(&p, C0_BASE));
    CHECK(!pbl_c0_vector_ok(NULL, C0_BASE));
    puts("PBL vector gate boundary/alignment tests: PASS");
    return 0;
}

int main(void)
{
    Fixture f;
    PblPort p;
    PblRecord selected;
    int boots;

    if (vector_gate() != 0) return 1;
    CHECK(pbl_crc32("123456789", 9u) == 0xCBF43926u);
    CHECK(PBL_MAX_TRIAL_BOOTS == 3u);   /* candidate policy, see layout.json */

    f = fixture(PBL_STATE_CONFIRMED, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.jumps == 1 && f.verified_package == 0xA0u && f.writes == 0);
    CHECK(f.jumped_vector == C0_BASE);

    /* CONFIRMED image whose vector is outside C0 never runs. */
    f = fixture(PBL_STATE_CONFIRMED, 0u); p = port(&f);
    image(&f, 0x00440000u, MSP_TOP, 0x00441001u);
    CHECK(pbl_run(&p) == PBL_RESULT_RECOVERY && f.jumps == 0 && f.recoveries == 1);

    f = fixture(PBL_STATE_ACTIVATING, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xA0u && f.writes == 0);

    /* Sec.15.6: ACTIVATING -> TRIAL(a=1) with exactly one write. */
    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xB0u && f.writes == 1);
    CHECK(f.copies[0].state == PBL_STATE_TRIAL &&
          f.copies[0].trial_boots == 1u && f.copies[0].sequence == 11u);
    CHECK(f.jumped_vector == C0_BASE);

    /* Whole trial: exactly PBL_MAX_TRIAL_BOOTS boots, then rollback. */
    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    for (boots = 0; boots < 10; ++boots) {
        new_boot(&f);
        if (pbl_run(&p) != PBL_RESULT_JUMP) break;
    }
    CHECK(boots == (int)PBL_MAX_TRIAL_BOOTS);
    CHECK(f.resets == 1 && f.swaps == 1);
    CHECK(f.writes == (int)PBL_MAX_TRIAL_BOOTS + 1);
    CHECK(f.copies[f.writes & 1 ? 0 : 1].state == PBL_STATE_ROLLING_BACK);

    /* Activation of an image that fails the gate: rollback, no TRIAL. */
    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    image(&f, C0_BASE + 0x200u, MSP_TOP, (C0_BASE + 0x1000u) | 1u);
    CHECK(pbl_run(&p) == PBL_RESULT_RESET && f.jumps == 0);
    CHECK(f.writes == 1 && f.copies[0].state == PBL_STATE_ROLLING_BACK);
    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    f.auth_ok = 0;
    CHECK(pbl_run(&p) == PBL_RESULT_RESET && f.jumps == 0 && f.swaps == 1);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.copies[0].trial_boots = PBL_MAX_TRIAL_BOOTS;
    pbl_record_seal(&f.copies[0]);
    CHECK(pbl_run(&p) == PBL_RESULT_RESET);
    CHECK(f.swaps == 1 && f.resets == 1 && f.writes == 1);
    CHECK(f.copies[0].state == PBL_STATE_ROLLING_BACK);

    /* A record claiming more trial boots than allowed is invalid. */
    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.copies[0].trial_boots = PBL_MAX_TRIAL_BOOTS + 1u;
    pbl_record_seal(&f.copies[0]);
    CHECK(pbl_run(&p) == PBL_RESULT_RECOVERY && f.jumps == 0);

    f = fixture(PBL_STATE_ROLLING_BACK, 0u); p = port(&f);
    CHECK(pbl_run(&p) == PBL_RESULT_JUMP);
    CHECK(f.verified_package == 0xA0u &&
           f.copies[0].state == PBL_STATE_CONFIRMED);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.auth_ok = 0;
    CHECK(pbl_run(&p) == PBL_RESULT_RESET && f.swaps == 1);

    f = fixture(PBL_STATE_TRIAL, 1u); p = port(&f);
    f.entry &= ~1u;   /* ARM-state reset handler would HardFault */
    CHECK(pbl_run(&p) == PBL_RESULT_RESET && f.swaps == 1 && f.jumps == 0);

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

    f = fixture(PBL_STATE_ACTIVATING, 1u); p = port(&f);
    f.write_ok = 0;
    CHECK(pbl_run(&p) == PBL_RESULT_RECOVERY && f.jumps == 0);
    puts("PBL host state-machine tests: PASS");
    return 0;
}
