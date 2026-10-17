/* S32K338 register-level services for the PBL (Q08).
 *
 * Register names, addresses and bit masks come from the RTD 6.0.0 S32K358
 * device headers (PBL_RTD_HEADER_DIR, not copied into this repository);
 * sequences mirror the RTD drivers or main-project code cited per function.
 * Items that could not be verified are TODO and fail closed. */

#include "pbl_hw.h"
#include "pbl_board_cfg.h"
#include "gen/pbl_layout.h"
#include "vrk_hse_channel.h"

#include "S32K358_FLEXCAN.h"
#include "S32K358_MC_ME.h"
#include "S32K358_MPU.h"
#include "S32K358_MU.h"
#include "S32K358_NVIC.h"
#include "S32K358_SCB.h"
#include "S32K358_SIUL2.h"
#include "S32K358_SWT.h"
#include "S32K358_SYSTICK.h"

#include <stddef.h>

#define DSB() __asm volatile ("dsb 0xF" ::: "memory")
#define ISB() __asm volatile ("isb 0xF" ::: "memory")

/* Siul2_Dio_Ip_Rev_Bit_16: pin i of a 16-pin half is PGPDO bit 15-i. */
static uint16_t rev16(uint16_t pins)
{
    uint16_t out = 0u;
    uint32_t i;
    for (i = 0u; i < 16u; ++i) {
        if ((pins & (1u << i)) != 0u) {
            out |= (uint16_t)(1u << (15u - i));
        }
    }
    return out;
}

/* Siul2_Port_Ip_SetOutputBuffer(base, pin, TRUE, mux). */
static void mscr_output(uint32_t index, uint32_t mux)
{
    IP_SIUL2->MSCR[index] &= ~SIUL2_MSCR_OBE_MASK;
    IP_SIUL2->MSCR[index] |= SIUL2_MSCR_OBE_MASK;
    IP_SIUL2->MSCR[index] &= ~PBL_MSCR_SSS_MASK;
    IP_SIUL2->MSCR[index] |= (mux & PBL_MSCR_SSS_MASK);
}

static bool gpio_output_low(uint32_t index)
{
    return (IP_SIUL2->MSCR[index] & (SIUL2_MSCR_OBE_MASK | PBL_MSCR_SSS_MASK)) ==
           SIUL2_MSCR_OBE_MASK;
}

/* Same order and checks as Vrk_BootForceHssOff (vrk_boot_clock.c): drive the
 * output latches low first, then enable the buffers, then verify. */
bool pbl_hw_safe_outputs(void)
{
    static const uint8_t bpin[3] = {0u, 4u, 11u};
    uint32_t i;
    IP_SIUL2->PGPDO1 &= (uint16_t)~rev16(PBL_HSS_PTA_H_PINS);
    IP_SIUL2->PGPDO2 &= (uint16_t)~rev16(PBL_HSS_PTB_L_PINS);
    mscr_output(PBL_MSCR_PTA_H_BASE + 13u, 0u);
    for (i = 0u; i < 3u; ++i) {
        mscr_output(PBL_MSCR_PTB_L_BASE + bpin[i], 0u);
    }
    IP_SIUL2->PGPDO1 &= (uint16_t)~rev16(PBL_HSS_PTA_H_PINS);
    IP_SIUL2->PGPDO2 &= (uint16_t)~rev16(PBL_HSS_PTB_L_PINS);
    if ((IP_SIUL2->PGPDO1 & rev16(PBL_HSS_PTA_H_PINS)) != 0u ||
        (IP_SIUL2->PGPDO2 & rev16(PBL_HSS_PTB_L_PINS)) != 0u ||
        !gpio_output_low(PBL_MSCR_PTA_H_BASE + 13u)) {
        return false;
    }
    for (i = 0u; i < 3u; ++i) {
        if (!gpio_output_low(PBL_MSCR_PTB_L_BASE + bpin[i])) {
            return false;
        }
    }
    return true;
}

/* Swt_Ip_Init order: unlock if soft locked, disable, clear TIF, configure,
 * timeout, enable. No hard lock, so the C0 startup can still take SWT_0 over
 * with its own unlock + disable (startup_cm7.s). */
void pbl_hw_swt_start(void)
{
    if ((IP_SWT_0->CR & SWT_CR_SLK_MASK) != 0u) {
        IP_SWT_0->SR = PBL_SWT_KEY_UNLOCK1;
        IP_SWT_0->SR = PBL_SWT_KEY_UNLOCK2;
    }
    IP_SWT_0->CR &= ~SWT_CR_WEN_MASK;
    IP_SWT_0->IR = SWT_IR_TIF_MASK;
    IP_SWT_0->CR = SWT_CR_MAP0_MASK | SWT_CR_MAP1_MASK | SWT_CR_MAP2_MASK |
                   SWT_CR_MAP3_MASK | SWT_CR_MAP4_MASK | SWT_CR_MAP5_MASK |
                   SWT_CR_MAP6_MASK | SWT_CR_MAP7_MASK | SWT_CR_RIA_MASK;
    IP_SWT_0->TO = PBL_SWT_TIMEOUT;
    IP_SWT_0->CR |= SWT_CR_WEN_MASK;
}

void pbl_hw_swt_service(void)
{
    IP_SWT_0->SR = PBL_SWT_KEY_SERVICE1;
    IP_SWT_0->SR = PBL_SWT_KEY_SERVICE2;
}

void pbl_hw_mpu_default(void)
{
    DSB();
    S32_MPU->CTRL = 0u;
    DSB();
    ISB();
}

#ifdef PBL_CAN_CLOCK_VERIFIED
/* MC_ME partition clock update, as the C0 startup does for MSCM
 * (startup_cm7.s InitMSCMClock): CLKEN, PUPD, key, inverted key, wait. */
static bool flexcan0_clock_gate(void)
{
    uint32_t n;
    if ((IP_MC_ME->PRTN1_COFB2_STAT & MC_ME_PRTN1_COFB2_STAT_BLOCK65_MASK) != 0u) {
        return true;
    }
    IP_MC_ME->PRTN1_COFB2_CLKEN |= MC_ME_PRTN1_COFB2_CLKEN_REQ65_MASK;
    IP_MC_ME->PRTN1_PUPD |= MC_ME_PRTN1_PUPD_PCUD_MASK;
    IP_MC_ME->CTL_KEY = MC_ME_CTL_KEY_KEY(0x5AF0u);
    IP_MC_ME->CTL_KEY = MC_ME_CTL_KEY_KEY(0xA50Fu);
    for (n = 0u; n < PBL_POLL_LIMIT; ++n) {
        if ((IP_MC_ME->PRTN1_PUPD & MC_ME_PRTN1_PUPD_PCUD_MASK) == 0u &&
            (IP_MC_ME->PRTN1_COFB2_STAT & MC_ME_PRTN1_COFB2_STAT_BLOCK65_MASK) != 0u) {
            return true;
        }
    }
    return false;
}
#endif

#define CAN0 ((FLEXCAN_Type *)IP_CAN_0_BASE)
/* Message buffers: FLEXCAN_IP_FEATURE_RAM_OFFSET 0x80, 16 bytes per classic
 * MB (CS, ID, DATA0, DATA1). MB0 = RX (any standard ID, filtered in SW),
 * MB1 = TX. Field positions and codes from RTD FlexCAN_Ip_HwAccess.h
 * (FLEXCAN_IP_CS_CODE_SHIFT 24, CS_SRR 0x400000, CS_IDE 0x200000,
 * CS_DLC_SHIFT 16, ID_STD_SHIFT 18, FLEXCAN_RX_EMPTY 4, TX_INACTIVE 8,
 * TX_DATA 0xC). */
#define CAN0_MB(n) ((volatile uint32_t *)(IP_CAN_0_BASE + 0x80u + 16u * (n)))
#define MB_CODE_RX_EMPTY 0x4u
#define MB_CODE_TX_INACTIVE 0x8u
#define MB_CODE_TX_DATA 0xCu
#define MB_CS_CODE(x) (((uint32_t)(x) & 0xFu) << 24)
#define MB_CS_DLC(x) (((uint32_t)(x) & 0xFu) << 16)
#define MB_CS_SRR (1u << 22)
#define MB_ID_STD(x) (((uint32_t)(x) & 0x7FFu) << 18)
#define CAN0_MB_COUNT 2u

#ifdef PBL_CAN_CLOCK_VERIFIED
static bool can_wait(uint32_t mask, bool set)
{
    uint32_t n;
    for (n = 0u; n < PBL_POLL_LIMIT; ++n) {
        if (((CAN0->MCR & mask) != 0u) == set) {
            return true;
        }
    }
    return false;
}

static void can0_pins_and_stb(void)
{
    IP_SIUL2->PGPDO7 &= (uint16_t)~rev16(PBL_CAN_STB_PTD_H_PINS);
    for (uint32_t pin = 5u; pin <= 8u; ++pin) {
        mscr_output(PBL_MSCR_PTD_H_BASE + pin, 0u);
    }
    mscr_output(PBL_CAN0_TX_MSCR, PBL_CAN0_TX_SSS);
    IP_SIUL2->MSCR[PBL_CAN0_RX_MSCR] |= SIUL2_MSCR_IBE_MASK;
    IP_SIUL2->IMCR[PBL_CAN0_RX_IMCR] = PBL_CAN0_RX_IMCR_SSS;
}
#endif

/* FlexCAN_Init / EnterFreezeMode / ClearRAM / ExitFreezeMode sequences
 * (FlexCAN_Ip_HwAccess.c), classic CAN, two message buffers. */
bool pbl_hw_can0_start(void)
{
#ifndef PBL_CAN_CLOCK_VERIFIED
    /* TODO(clock): FLEXCANA clock tree not configured; keep CAN0 off. */
    return false;
#else
    uint32_t i;
    if (!flexcan0_clock_gate()) {
        return false;
    }
    CAN0->MCR |= FLEXCAN_MCR_FRZ_MASK;
    CAN0->MCR |= FLEXCAN_MCR_HALT_MASK;
    CAN0->MCR &= ~FLEXCAN_MCR_MDIS_MASK;
    if (!can_wait(FLEXCAN_MCR_FRZACK_MASK, true)) {
        return false;
    }
    CAN0->MCR |= FLEXCAN_MCR_SOFTRST_MASK;
    if (!can_wait(FLEXCAN_MCR_SOFTRST_MASK, false)) {
        return false;
    }
    CAN0->MCR |= FLEXCAN_MCR_AEN_MASK;
    CAN0->CTRL2 |= FLEXCAN_CTRL2_WRMFRZ_MASK;
    for (i = 0u; i < CAN0_MB_COUNT * 4u; ++i) {
        CAN0_MB(0)[i] = 0u;
    }
    for (i = 0u; i < CAN0_MB_COUNT; ++i) {
        CAN0->RXIMR[i] = 0u;           /* accept all; IDs filtered in SW */
    }
    CAN0->RXMGMASK = 0u;
    CAN0->CTRL2 &= ~FLEXCAN_CTRL2_WRMFRZ_MASK;
    CAN0->IMASK1 = 0u;
    CAN0->IFLAG1 = 0xFFFFFFFFu;
    CAN0->MCR = (CAN0->MCR & ~(FLEXCAN_MCR_MAXMB_MASK | FLEXCAN_MCR_FDEN_MASK)) |
                FLEXCAN_MCR_MAXMB(CAN0_MB_COUNT - 1u) | FLEXCAN_MCR_SRXDIS_MASK |
                FLEXCAN_MCR_IRMQ_MASK;
    CAN0->CBT = FLEXCAN_CBT_BTF_MASK | FLEXCAN_CBT_EPRESDIV(PBL_CAN0_EPRESDIV) |
                FLEXCAN_CBT_ERJW(PBL_CAN0_ERJW) | FLEXCAN_CBT_EPROPSEG(PBL_CAN0_EPROPSEG) |
                FLEXCAN_CBT_EPSEG1(PBL_CAN0_EPSEG1) | FLEXCAN_CBT_EPSEG2(PBL_CAN0_EPSEG2);
    CAN0_MB(0)[0] = MB_CS_CODE(MB_CODE_RX_EMPTY);
    CAN0_MB(1)[0] = MB_CS_CODE(MB_CODE_TX_INACTIVE);
    can0_pins_and_stb();
    CAN0->MCR &= ~FLEXCAN_MCR_HALT_MASK;
    CAN0->MCR &= ~FLEXCAN_MCR_FRZ_MASK;
    return can_wait(FLEXCAN_MCR_FRZACK_MASK, false);
#endif
}

bool pbl_hw_can0_receive(uint32_t *id, uint8_t *data, uint32_t *dlc)
{
    volatile uint32_t *mb = CAN0_MB(0);
    uint32_t cs, w0, w1, i;
    if ((CAN0->IFLAG1 & 1u) == 0u) {
        return false;
    }
    cs = mb[0];                       /* reading CS locks the MB */
    *id = (mb[1] >> 18) & 0x7FFu;
    w0 = mb[2];
    w1 = mb[3];
    *dlc = (cs >> 16) & 0xFu;
    for (i = 0u; i < 4u; ++i) {        /* MB data is big endian per word */
        data[i] = (uint8_t)(w0 >> (24u - 8u * i));
        data[4u + i] = (uint8_t)(w1 >> (24u - 8u * i));
    }
    (void)CAN0->TIMER;                 /* unlock */
    CAN0->IFLAG1 = 1u;
    mb[0] = MB_CS_CODE(MB_CODE_RX_EMPTY);
    return (cs & (1u << 21)) == 0u && *dlc <= 8u;   /* IDE=0: standard only */
}

bool pbl_hw_can0_send(uint32_t id, const uint8_t *data)
{
    volatile uint32_t *mb = CAN0_MB(1);
    uint32_t w0 = 0u, w1 = 0u, i, n;
    for (i = 0u; i < 4u; ++i) {
        w0 |= (uint32_t)data[i] << (24u - 8u * i);
        w1 |= (uint32_t)data[4u + i] << (24u - 8u * i);
    }
    mb[0] = MB_CS_CODE(MB_CODE_TX_INACTIVE);
    mb[1] = MB_ID_STD(id);
    mb[2] = w0;
    mb[3] = w1;
    mb[0] = MB_CS_CODE(MB_CODE_TX_DATA) | MB_CS_DLC(8u) | MB_CS_SRR;
    for (n = 0u; n < PBL_POLL_LIMIT; ++n) {
        if ((CAN0->IFLAG1 & 2u) != 0u) {
            CAN0->IFLAG1 = 2u;
            return true;
        }
    }
    return false;
}

/* HSE service descriptor (hse_interface.h hseSrvDescriptor_t: srvId,
 * 4-byte metadata, service union; hseGetAttrSrv_t: attrId u16, 2 reserved,
 * attrLen u32, pAttr 32-bit host address). Lives in the PBL SRAM window
 * (no cache in the PBL), never in TCM. */
typedef struct {
    uint32_t srv_id;
    uint8_t meta[4];
    uint16_t attr_id;
    uint8_t reserved[2];
    uint32_t attr_len;
    uint32_t p_attr;
    uint32_t pad[8];
} PblHseDescriptor;

static volatile PblHseDescriptor hse_desc;
static volatile uint8_t hse_attr[16];

/* One channel latch guards the shared hse_desc/hse_attr buffers (LH-R1-005):
 * a poll timeout latches the channel unavailable so a late HSE write can never
 * corrupt a reused buffer; only a reliable reset clears it. */
static VrkHseChannel hse_channel = { VRK_HSE_CH_READY };

static bool hse_request(uint32_t srv_id, uint32_t attr_id, uint32_t length)
{
    const uint32_t ch = 1u << PBL_HSE_MU_CHANNEL;
    uint32_t n, i;
    if (!vrk_hse_ch_available(&hse_channel)) {
        return false;   /* channel latched unavailable after a prior timeout */
    }
    if (((IP_MU_0__MUB->FSR >> 16) & PBL_HSE_STATUS_INIT_OK) == 0u ||
        (IP_MU_0__MUB->TSR & ch) == 0u || (IP_MU_0__MUB->RSR & ch) != 0u) {
        return false;
    }
    if (!vrk_hse_ch_begin(&hse_channel)) {
        return false;   /* defensive: a request is already outstanding */
    }
    for (i = 0u; i < sizeof(hse_attr); ++i) {
        hse_attr[i] = 0u;
    }
    hse_desc.srv_id = srv_id;
    hse_desc.attr_id = (uint16_t)attr_id;
    hse_desc.attr_len = length;
    hse_desc.p_attr = (uint32_t)(uintptr_t)hse_attr;
    DSB();
    IP_MU_0__MUB->TR[PBL_HSE_MU_CHANNEL] = (uint32_t)(uintptr_t)&hse_desc;
    for (n = 0u; n < PBL_POLL_LIMIT; ++n) {
        if ((IP_MU_0__MUB->RSR & ch) != 0u) {
            uint32_t rsp = IP_MU_0__MUB->RR[PBL_HSE_MU_CHANNEL];
            DSB();
            vrk_hse_ch_complete(&hse_channel);
            return rsp == PBL_HSE_SRV_RSP_OK;
        }
    }
    vrk_hse_ch_fail(&hse_channel);   /* timeout: fail closed, forbid buffer reuse */
    return false;
}

bool pbl_hw_hse_get_attr(uint32_t attr_id, void *out, uint32_t length)
{
    uint8_t *dst = (uint8_t *)out;
    uint32_t i;
    if (out == NULL || length == 0u || length > sizeof(hse_attr) ||
        !hse_request(PBL_HSE_SRV_ID_GET_ATTR, attr_id, length)) {
        return false;
    }
    for (i = 0u; i < length; ++i) {
        dst[i] = hse_attr[i];
    }
    return true;
}

/* HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK: no data structure, SuperUser rights.
 * TODO(HSE): SuperUser access for the PBL (life cycle / SU configuration). */
bool pbl_hw_hse_activate_passive(void)
{
    return hse_request(PBL_HSE_SRV_ID_ACTIVATE_PASSIVE, 0u, 0u);
}

/* Power_Ip_MC_ME_SocTriggerResetEvent(POWER_IP_FUNC_RESET_MODE). */
void pbl_hw_functional_reset(void)
{
    IP_MC_ME->MODE_CONF = MC_ME_MODE_CONF_FUNC_RST_MASK;
    IP_MC_ME->MODE_UPD = MC_ME_MODE_UPD_MODE_UPD_MASK;
    IP_MC_ME->CTL_KEY = MC_ME_CTL_KEY_KEY(0x5AF0u);
    IP_MC_ME->CTL_KEY = MC_ME_CTL_KEY_KEY(0xA50Fu);
    for (;;) {
        __asm volatile ("wfi");
    }
}

/* Hand-off to C0 (design Sec.3.2): interrupts stay disabled, SysTick off,
 * every NVIC enable and pending bit cleared, MPU back to the default map,
 * VTOR = C0 vector table, MSP = its first word, branch to its reset handler.
 * pbl_c0_vector_ok() has validated the table before this point. */
void pbl_hw_jump(uint32_t vector)
{
    const volatile uint32_t *table = (const volatile uint32_t *)(uintptr_t)vector;
    uint32_t i, msp, entry;
    __asm volatile ("cpsid i" ::: "memory");
    S32_SysTick->CSRr = 0u;
    for (i = 0u; i < S32_NVIC_ICER_COUNT; ++i) {
        S32_NVIC->ICER[i] = 0xFFFFFFFFu;
        S32_NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    pbl_hw_mpu_default();
    pbl_hw_swt_service();
    S32_SCB->VTOR = vector;
    DSB();
    ISB();
    msp = table[0];
    entry = table[1];
    __asm volatile (
        "msr msp, %0\n"
        "isb 0xF\n"
        "bx %1\n"
        : : "r" (msp), "r" (entry) : "memory");
    for (;;) {
    }
}
