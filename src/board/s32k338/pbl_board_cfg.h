#ifndef PBL_BOARD_CFG_H
#define PBL_BOARD_CFG_H

/* S32K338 / HCM_VRK_3H board constants for the PBL. Every value is taken from
 * the main project (HCM_VRK_3H, branch m0/baseline-t00-fota-layout) or from the
 * RTD 6.0.0 headers; the source is cited per value. Nothing here is invented:
 * unknown items are TODO and fail closed. */

/* --- Safe outputs: HSS drivers off (vrk_boot_clock.c Vrk_BootForceHssOff) ---
 * PTA29 = PTA_H_HALF pin 13 (MSCR[29]), PTB0/4/11 = PTB_L_HALF pins 0/4/11
 * (MSCR[32+n]). PTA_H_HALF = SIUL2 PGPDO1, PTB_L_HALF = PGPDO2
 * (generate/include/Siul2_Dio_Ip_Cfg.h). PGPDO is bit reversed: pin i is
 * bit 15-i (Siul2_Dio_Ip.c Siul2_Dio_Ip_Rev_Bit_16). */
#define PBL_HSS_PTA_H_PINS   (1u << 13)                      /* VRK_BOOT_HSS_A_MASK */
#define PBL_HSS_PTB_L_PINS   ((1u << 0) | (1u << 4) | (1u << 11)) /* 0x0811 */
#define PBL_MSCR_PTA_H_BASE  16u
#define PBL_MSCR_PTB_L_BASE  32u
#define PBL_MSCR_PTD_H_BASE  112u
/* MSCR SSS field width as checked by Vrk_BootForceHssOff (0xF). */
#define PBL_MSCR_SSS_MASK    0xFu

/* --- CAN0 transceiver TJA1044 STB (vrk_core0_bsp.c): PTD21..PTD24 =
 * PTD_H_HALF pins 5..8 (PGPDO7), low = normal mode. */
#define PBL_CAN_STB_PTD_H_PINS 0x01E0u

/* --- FlexCAN0 pins (generate/src/Siul2_Port_Ip_PBcfg.c, Port_Cfg.h):
 * CAN0_TX = PTB1, MSCR[33], ALT5 (SIUL2_0_PORT33_CAN0_CAN0_TX_OUT), OBE=1.
 * CAN0_RX = PTA28, MSCR[28] IBE=1, IMCR[0] = ALT4. */
#define PBL_CAN0_TX_MSCR     33u
#define PBL_CAN0_TX_SSS      5u
#define PBL_CAN0_RX_MSCR     28u
#define PBL_CAN0_RX_IMCR     0u
#define PBL_CAN0_RX_IMCR_SSS 4u

/* --- FlexCAN0 nominal bit timing inherited from the main project
 * (FlexCAN_Ip_PBcfg.c: presc 0, propseg 0, pseg1 29, pseg2 7, rjw 7, CBT
 * format because FD is enabled there). Valid only with the main project's
 * FLEXCANA clock (FXOSC 40 MHz path, Clock_Ip_PBcfg.c). */
#define PBL_CAN0_EPRESDIV    0u
#define PBL_CAN0_ERJW        7u
#define PBL_CAN0_EPROPSEG    0u
#define PBL_CAN0_EPSEG1      29u
#define PBL_CAN0_EPSEG2      7u
/* TODO(clock): the PBL does not yet bring up FXOSC and the MC_CGM FLEXCANA
 * mux/divider. Until that sequence is ported from Clock_Ip_PBcfg.c and the
 * bit rate is measured on the bench, CAN0 stays disabled (no bus traffic at a
 * wrong bit rate). Define PBL_CAN_CLOCK_VERIFIED only after that review. */

/* --- Recovery diagnostic IDs (main project vrk_uds_cfg.h). LH/RH coding is
 * not known in the PBL, so both physical IDs are served on their own TX ID.
 * TODO(OEM): confirm the PBL recovery IDs against the ODX. */
#define PBL_UDS_LH_RX_ID     0x6C9u
#define PBL_UDS_RH_RX_ID     0x6CAu
#define PBL_UDS_LH_TX_ID     0x649u
#define PBL_UDS_RH_TX_ID     0x64Au
#define PBL_UDS_FUNC_RX_ID   0x6FFu
#define PBL_UDS_PADDING      0xCCu  /* VRK_UDS_PADDING_VALUE */

/* --- SWT_0 (Swt_43_Instance0_Ip_PBcfg.c / startup_cm7.s): service keys
 * 0xA602/0xB480, unlock 0xC520/0xD928, timeout 0x3E80 = 500 ms of the SWT
 * clock as configured for C0. ITR=0 so a timeout resets directly (the PBL has
 * no SWT interrupt handler). */
#define PBL_SWT_KEY_SERVICE1 0xA602u
#define PBL_SWT_KEY_SERVICE2 0xB480u
#define PBL_SWT_KEY_UNLOCK1  0xC520u
#define PBL_SWT_KEY_UNLOCK2  0xD928u
#define PBL_SWT_TIMEOUT      0x3E80u

/* --- Cortex-M7 TCM control (startup_cm7.s CM7_DTCMCR/CM7_ITCMCR). */
#define PBL_CM7_ITCMCR       0xE000EF90u
#define PBL_CM7_DTCMCR       0xE000EF94u

/* --- HSE (HSE FW 2.55.0 interface headers, hse_interface.h,
 * hse_srv_attr.h, hse_srv_responses.h, hse_status_and_errors.h). The PBL uses
 * MU0 channel 1 (channel 0 is the C0 probe channel, vrk_hse_probe.c). */
#define PBL_HSE_MU_CHANNEL                1u
#define PBL_HSE_SRV_ID_GET_ATTR           0x00A50002u
#define PBL_HSE_SRV_ID_ACTIVATE_PASSIVE   0x00000051u
#define PBL_HSE_FW_VERSION_ATTR_ID        1u
#define PBL_HSE_FW_PARTITION_ATTR_ID      102u
#define PBL_HSE_FW_PARTITION_PRIMARY      1u
#define PBL_HSE_FW_PARTITION_BACKUP       2u
#define PBL_HSE_SRV_RSP_OK                0x55A5AA33u
#define PBL_HSE_STATUS_INIT_OK            (1u << 8)    /* in FSR[31:16] */
/* Bounded poll count for HSE/C40/FlexCAN handshakes (no timer in the PBL);
 * the SWT bounds the wall time. TODO(timing): replace by an STM time base. */
#define PBL_POLL_LIMIT                    2000000u

/* TODO(M09/NXP): HSE 2.55 has no documented attribute for the physical
 * (A/B) block. HSE_FW_PARTITION_ATTR_ID is a *candidate* source only; the
 * mapping is unverified and therefore disabled. Without it physical_bank()
 * fails and the PBL stays in recovery (fail closed). */
/* #define PBL_HSE_BANK_FROM_FW_PARTITION_VERIFIED */

/* TODO(M10/C40): Mem_43_INFLS domain ID for CM7_0 (MCR.PEID check of
 * C40_Ip_MainInterfaceWriteJobAddress) and the sector lock map are not
 * configured in this project. Without PBL_C40_DOMAIN_ID every C40 job fails. */
/* #define PBL_C40_DOMAIN_ID 0u */

#endif
