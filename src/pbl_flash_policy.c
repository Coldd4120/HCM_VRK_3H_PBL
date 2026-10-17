#include "pbl_flash_policy.h"
#include "gen/pbl_layout.h"

#define WINDOW_START PBL_LAYOUT_DESCRIPTOR_PASSIVE_BASE
#define WINDOW_END   (PBL_LAYOUT_C2_PASSIVE_BASE + PBL_LAYOUT_C2_SIZE)

#if (PBL_LAYOUT_DESCRIPTOR_END != PBL_LAYOUT_C0_BASE) || \
    (PBL_LAYOUT_C0_END != PBL_LAYOUT_C1_BASE) || (PBL_LAYOUT_C1_END != PBL_LAYOUT_C2_BASE)
#error "whitelist assumes descriptor, C0, C1, C2 are contiguous"
#endif

static bool in_window(uint32_t address, uint32_t length)
{
    return length != 0u && address >= WINDOW_START && address < WINDOW_END &&
           length <= (WINDOW_END - address);
}

bool pbl_flash_program_allowed(uint32_t address, uint32_t length)
{
    return in_window(address, length) &&
           (address % PBL_FLASH_PROGRAM_UNIT) == 0u &&
           (length % PBL_FLASH_PROGRAM_UNIT) == 0u &&
           length <= PBL_FLASH_PAGE_BYTES &&
           (address / PBL_FLASH_PAGE_BYTES) ==
               ((address + length - 1u) / PBL_FLASH_PAGE_BYTES);
}

bool pbl_flash_erase_allowed(uint32_t address, uint32_t length)
{
    return in_window(address, length) &&
           (address % PBL_LAYOUT_PFLASH_SECTOR) == 0u &&
           (length % PBL_LAYOUT_PFLASH_SECTOR) == 0u;
}
