#include "pbl_uds.h"
#include "pbl_flash_policy.h"
#include "gen/pbl_layout.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static uint32_t req(PblUds *u, const char *bytes, uint32_t n, bool functional,
                    uint32_t now, uint8_t *rsp)
{
    memset(rsp, 0xEE, 8u);
    return pbl_uds_process(u, (const uint8_t *)bytes, n, functional, now, rsp, 8u);
}

static int uds(void)
{
    static const PblUdsInfo info = {0x5222308Bu, 0x00030000u, 2u, 1u, 3u};
    PblUds u;
    uint8_t r[8];
    pbl_uds_init(&u, &info, 0u);

    CHECK(req(&u, "\x10\x02", 2u, false, 10u, r) == 6u);
    CHECK(r[0] == 0x50u && r[1] == 0x02u && u.session == 0x02u);
    CHECK(r[3] == 50u && r[4] == 0x01u && r[5] == 0xF4u);
    CHECK(req(&u, "\x10\x83", 2u, false, 20u, r) == 0u && u.session == 0x03u);
    CHECK(req(&u, "\x10\x04", 2u, false, 30u, r) == 3u && r[0] == 0x7Fu && r[2] == 0x12u);
    CHECK(req(&u, "\x10\x04", 2u, true, 30u, r) == 0u);          /* functional SFNS */
    CHECK(req(&u, "\x10", 1u, false, 30u, r) == 3u && r[2] == 0x13u);
    CHECK(req(&u, "\x10\x01\x00", 3u, false, 30u, r) == 3u && r[2] == 0x13u);

    CHECK(req(&u, "\x3E\x00", 2u, false, 40u, r) == 2u && r[0] == 0x7Eu);
    CHECK(req(&u, "\x3E\x80", 2u, true, 40u, r) == 0u);
    CHECK(req(&u, "\x3E\x01", 2u, false, 40u, r) == 3u && r[2] == 0x12u);

    CHECK(req(&u, "\x22\xF1\x86", 3u, false, 50u, r) == 4u && r[3] == 0x03u);
    CHECK(req(&u, "\x22\xFD\x00", 3u, false, 50u, r) == 6u &&
          r[3] == 2u && r[4] == 1u && r[5] == 3u);
    CHECK(req(&u, "\x22\xFD\x01", 3u, false, 50u, r) == 7u &&
          r[3] == 0x52u && r[4] == 0x22u && r[5] == 0x30u && r[6] == 0x8Bu);
    CHECK(req(&u, "\x22\xFD\x02", 3u, false, 50u, r) == 7u && r[4] == 0x03u);
    CHECK(req(&u, "\x22\x12\x34", 3u, false, 50u, r) == 3u && r[2] == 0x31u);
    CHECK(req(&u, "\x22\x12\x34", 3u, true, 50u, r) == 0u);
    CHECK(req(&u, "\x22\xF1\x86\xF1\x86", 5u, false, 50u, r) == 3u && r[2] == 0x13u);

    /* Download path stays closed in this stage. */
    CHECK(req(&u, "\x34\x00\x44\x00\x84\x00\x00\x00\x00\x10\x00", 11u, false, 60u, r) == 3u &&
          r[1] == 0x34u && r[2] == 0x22u);
    CHECK(req(&u, "\x36\x01\xAA", 3u, false, 60u, r) == 3u && r[2] == 0x22u);
    CHECK(req(&u, "\x37", 1u, false, 60u, r) == 3u && r[2] == 0x22u);
    CHECK(req(&u, "\x31\x01\xFF\x00", 4u, false, 60u, r) == 3u && r[2] == 0x22u);
    CHECK(req(&u, "\x27\x01", 2u, false, 60u, r) == 3u && r[2] == 0x11u);
    CHECK(req(&u, "\x27\x01", 2u, true, 60u, r) == 0u);

    /* S3: back to default after 5 s without a request. */
    pbl_uds_tick(&u, 60u + PBL_UDS_S3_MS - 1u);
    CHECK(u.session == 0x03u);
    pbl_uds_tick(&u, 60u + PBL_UDS_S3_MS);
    CHECK(u.session == PBL_UDS_SESSION_DEFAULT);

    CHECK(!u.reset_requested);
    CHECK(req(&u, "\x11\x02", 2u, false, 70u, r) == 3u && r[2] == 0x12u && !u.reset_requested);
    CHECK(req(&u, "\x11\x01", 2u, false, 70u, r) == 2u && r[0] == 0x51u && u.reset_requested);
    CHECK(pbl_uds_process(&u, (const uint8_t *)"\x3E\x00", 2u, false, 0u, r, 6u) == 0u);
    puts("PBL recovery UDS subset tests: PASS");
    return 0;
}

static int isotp(void)
{
    const uint8_t sf[8] = {0x02u, 0x10u, 0x02u, 0xCCu, 0xCCu, 0xCCu, 0xCCu, 0xCCu};
    const uint8_t ff[8] = {0x10u, 0x0Au, 0x36u, 0x01u, 0, 0, 0, 0};
    const uint8_t sf0[8] = {0x00u};
    const uint8_t sf8[8] = {0x08u};
    const uint8_t *p = NULL;
    uint8_t out[8];
    CHECK(pbl_isotp_sf_decode(sf, 8u, &p) == 2u && p == &sf[1]);
    CHECK(pbl_isotp_sf_decode(sf, 2u, &p) == 0u);   /* DLC shorter than PCI */
    CHECK(pbl_isotp_sf_decode(ff, 8u, &p) == 0u);   /* multi-frame ignored */
    CHECK(pbl_isotp_sf_decode(sf0, 8u, &p) == 0u);
    CHECK(pbl_isotp_sf_decode(sf8, 8u, &p) == 0u);
    CHECK(pbl_isotp_sf_decode(sf, 9u, &p) == 0u);
    CHECK(pbl_isotp_sf_encode((const uint8_t *)"\x50\x02\x00\x32\x01\xF4", 6u, out, 0xCCu));
    CHECK(out[0] == 6u && out[1] == 0x50u && out[6] == 0xF4u && out[7] == 0xCCu);
    CHECK(!pbl_isotp_sf_encode((const uint8_t *)"12345678", 8u, out, 0xCCu));
    CHECK(!pbl_isotp_sf_encode((const uint8_t *)"1", 0u, out, 0xCCu));
    puts("PBL ISO-TP single-frame tests: PASS");
    return 0;
}

static int policy(void)
{
    const uint32_t lo = PBL_LAYOUT_DESCRIPTOR_PASSIVE_BASE;
    const uint32_t hi = PBL_LAYOUT_C2_PASSIVE_BASE + PBL_LAYOUT_C2_SIZE;
    const uint32_t s = PBL_LAYOUT_PFLASH_SECTOR;
    CHECK(pbl_flash_erase_allowed(lo, s));
    CHECK(pbl_flash_erase_allowed(PBL_LAYOUT_C0_PASSIVE_BASE, PBL_LAYOUT_C0_SIZE));
    CHECK(pbl_flash_erase_allowed(lo, hi - lo));
    CHECK(pbl_flash_erase_allowed(hi - s, s));
    CHECK(!pbl_flash_erase_allowed(hi, s));                          /* spare */
    CHECK(!pbl_flash_erase_allowed(hi - s, 2u * s));
    CHECK(!pbl_flash_erase_allowed(lo - s, s));                      /* PBL copy */
    CHECK(!pbl_flash_erase_allowed(PBL_LAYOUT_PFLASH_PASSIVE_BASE, s));
    CHECK(!pbl_flash_erase_allowed(PBL_LAYOUT_HSE_TAIL_PASSIVE_BASE, s));
    CHECK(!pbl_flash_erase_allowed(PBL_LAYOUT_HSE_TAIL_BASE, s));
    CHECK(!pbl_flash_erase_allowed(PBL_LAYOUT_C0_BASE, s));          /* active */
    CHECK(!pbl_flash_erase_allowed(PBL_LAYOUT_PBL_BASE, s));
    CHECK(!pbl_flash_erase_allowed(lo + 0x1000u, s));                /* misaligned */
    CHECK(!pbl_flash_erase_allowed(lo, s + 0x1000u));
    CHECK(!pbl_flash_erase_allowed(lo, 0u));
    CHECK(!pbl_flash_erase_allowed(lo, 0xFFFFE000u));                /* wraps */
    CHECK(!pbl_flash_erase_allowed(0xFFFFE000u, s));

    CHECK(pbl_flash_program_allowed(lo, 8u));
    CHECK(pbl_flash_program_allowed(lo, 128u));
    CHECK(pbl_flash_program_allowed(hi - 8u, 8u));
    CHECK(!pbl_flash_program_allowed(hi - 8u, 16u));
    CHECK(!pbl_flash_program_allowed(lo + 120u, 16u));              /* crosses page */
    CHECK(!pbl_flash_program_allowed(lo, 136u));
    CHECK(!pbl_flash_program_allowed(lo + 4u, 8u));
    CHECK(!pbl_flash_program_allowed(lo, 12u));
    CHECK(!pbl_flash_program_allowed(lo - 8u, 8u));
    CHECK(!pbl_flash_program_allowed(PBL_LAYOUT_C0_BASE, 8u));
    CHECK(!pbl_flash_program_allowed(PBL_LAYOUT_HSE_TAIL_PASSIVE_BASE, 8u));
    CHECK(!pbl_flash_program_allowed(0xFFFFFFF8u, 8u));
    puts("PBL recovery flash whitelist tests: PASS");
    return 0;
}

int main(void)
{
    return (uds() || isotp() || policy()) ? 1 : 0;
}
