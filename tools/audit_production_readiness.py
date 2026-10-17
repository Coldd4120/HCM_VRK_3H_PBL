"""Audit whether the current VRK board sources can release a PBL flash image.

Read-only source audit. A zero exit status means only that these static gates
passed; HSE installation and target-board evidence remain separate gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

PBL = Path(__file__).resolve().parents[1]
DEFAULT_APP = PBL.parent / "HCM_VRK_3H"


def read(root: Path, relative: str) -> tuple[Path, str]:
    path = root / relative
    return path, path.read_text(encoding="utf-8", errors="replace")


def item(name: str, ok: bool, evidence: str, action: str) -> dict:
    return {"gate": name, "passed": ok, "evidence": evidence, "required_action": action}


def audit(app: Path) -> dict:
    profile_path = PBL / "config" / "board_profile.json"
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    linker, ld = read(app, "S32K338_VRK_M7_0_0/Project_Settings/Linker_Files/"
                           "linker_flash_c0_s32k338.ld")
    startup, start = read(app, "S32K338_VRK_M7_0_0/Project_Settings/Startup_Code/"
                               "startup_cm7.s")
    clock, boot = read(app, "S32K338_VRK_M7_0_0/src/platform/vrk_boot_clock.c")
    fota, fota_text = read(app, "S32K338_VRK_M7_0_0/src/platform/vrk_core0_fota.c")
    probe, probe_text = read(app, "S32K338_VRK_M7_0_0/src/platform/vrk_hse_probe.h")
    pbl_port, port_text = read(PBL, "src/pbl_board_unbound.c")
    board_port, board_text = read(PBL, "src/board/s32k338/pbl_board_s32k338.c")
    layout_json, layout_text = read(app, "common/boot/layout.json")
    c0_mem, c0_mem_text = read(app, "S32K338_VRK_M7_0_0/Project_Settings/Linker_Files/gen/"
                                    "layout_mem.ld")
    pbl_gen, pbl_gen_text = read(PBL, "linker/gen/pbl_layout.ld")
    pbl_linker, pbl_ld = read(PBL, "linker/pbl_s32k338.ld")
    pbl_startup, pbl_start = read(PBL, "startup/pbl_startup.S")
    can_cfg, can_text = read(app, "S32K338_VRK_M7_0_0/generate/src/Can_43_FLEXCAN_PBcfg.c")
    bsp, bsp_text = read(app, "S32K338_VRK_M7_0_0/src/platform/vrk_core0_bsp.c")
    checks = []

    can_profile_ok = (profile["target_derivative"] == "S32K338" and
                      profile["recovery_can_controller"] == "FlexCAN0" and
                      "FLEXCAN_0_BASE" in can_text and
                      "SPI_CH_OKX0210" in bsp_text and "TJA1044" in bsp_text)
    checks.append(item("prototype board CAN0/SBC source match", can_profile_ok,
                       "S32K338 + FlexCAN0 + OKX0210/SPI + TJA1044 STB found" if can_profile_ok else
                       "board profile differs from current C0 CAN/SBC source",
                       "Resolve the actual board revision, exact chip order code and CAN pin/transceiver mapping; validate UJA1169 separately."))

    def region(text: str, name: str):
        match = re.search(rf"\b{name}\b[^:]*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)\s*,"
                          rf"\s*LENGTH\s*=\s*(0x[0-9A-Fa-f]+)", text)
        return (int(match.group(1), 16), int(match.group(2), 16)) if match else None

    # C0 and PBL regions come from the generated layout files (INCLUDEd by
    # both linker scripts); the scripts must not carry literal origins.
    c0 = region(c0_mem_text, "int_flash_c0")
    pbl = region(pbl_gen_text, "PBL_FLASH")
    includes = ("gen/layout_mem.ld" in ld and "gen/pbl_layout.ld" in pbl_ld and
                not re.search(r"ORIGIN\s*=\s*0x", pbl_ld))
    separated = (includes and c0 is not None and pbl is not None and
                 (c0[0] >= pbl[0] + pbl[1] or pbl[0] >= c0[0] + c0[1]))
    checks.append(item("C0 image separated from PBL", separated,
                       "C0 {} / PBL {} from generated layout, INCLUDE ok={}".format(
                           *(f"0x{r[0]:08X}+0x{r[1]:X}" if r else "missing" for r in (c0, pbl)),
                           includes),
                       "Move the C0 APP linker and boot vector to the frozen post-PBL window; rebuild all cores."))

    # startup_cm7.s is RTD/S32DS generated and still emits .boot_header; the
    # C0 linker must discard it and keep no KEEP(*(.boot_header)).
    app_emits_ivt = '.section ".boot_header"' in start
    app_links_ivt = bool(re.search(r"KEEP\s*\(\s*\*\s*\(\s*\.boot_header", ld))
    app_discards_ivt = bool(re.search(r"/DISCARD/\s*:\s*\{[^}]*\*\s*\(\s*\.boot_header\s*\)", ld))
    app_owns_ivt = app_links_ivt or (app_emits_ivt and not app_discards_ivt)
    checks.append(item("single SBAF boot header owner", not app_owns_ivt,
                       "APP linker keeps .boot_header" if app_owns_ivt else
                       "APP .boot_header discarded by the C0 linker; PBL owns the IVT",
                       "Remove the SBAF boot header from APP and retain its application vector for PBL handoff."))

    hardcoded = all(value in boot for value in ("0x00402000UL", "0x00500000UL", "0x00600000UL"))
    checks.append(item("three-core entry contract migrated", not hardcoded,
                       "APP boot code still asserts old C0/C1/C2 entry addresses" if hardcoded else
                       "Old three-core entry triple not found",
                       "Use a frozen package manifest/link map for C0, C1 and C2 entry addresses."))

    weak = re.findall(r"VRK_FOTA_WEAK\s+(?:Std_ReturnType|void)\s+Vrk_FotaPort_(\w+)\s*\(", fota_text)
    checks.append(item("internal flash and HSE ports bound", not weak,
                       f"weak FOTA default ports still present: {', '.join(weak)}" if weak else
                       "No weak FOTA default ports found",
                       "Implement and link the bank query, verified package, record, flash and HSE swap ports; remove duplicate activation authority."))

    hse_disabled = bool(re.search(r"^#define\s+VRK_HSE_PROBE_ENABLE\s+0\b", probe_text, re.M))
    checks.append(item("HSE evidence enabled", not hse_disabled,
                       "APP HSE probe defaults to disabled" if hse_disabled else
                       "HSE probe is not default-disabled",
                       "Integrate the verified HSE service owner and prove AB_SWAP mode/version on the target."))

    board_todo = "#error \"PBL_PRODUCTION: s32k338 port still has fail-closed TODOs" in board_text
    checks.append(item("PBL board port implemented", not board_todo,
                       "s32k338 board port present but production-blocked by fail-closed TODOs "
                       "(HSE bank attribute, D-Flash record, package verify, CAN clock)" if board_todo else
                       "s32k338 production guard absent",
                       "Implement safety I/O, HSE mapping/authentication, persistent record, jump/reset and CAN recovery in the PBL project."))

    short_vectors = not (".rept 233" in pbl_start and ".section .isr_vector" in pbl_start and
                         "SIZEOF(.isr_vector) == 249 * 4" in pbl_ld)
    checks.append(item("PBL board startup and IRQ vectors", not short_vectors,
                       "PBL startup lacks the full 249 entry (16+233) vector table" if short_vectors else
                       "Full 249 entry (16+233) vector table, size asserted by the PBL linker",
                       "Integrate validated S32K338 startup, RAM ECC/MPU/VTOR initialization and every enabled CAN/timer IRQ vector."))

    layout_gate = app / "tools" / "check_fota_layout.py"
    if layout_gate.is_file():
        run = subprocess.run([sys.executable, str(layout_gate), "--require-external",
                              "--mode", "production", "--pbl-root", str(PBL)],
                             cwd=app, capture_output=True, text=True, check=False)
        layout_ok = run.returncode == 0
        layout_evidence = (run.stdout if layout_ok else run.stderr).strip()
    else:
        layout_ok = False
        layout_evidence = "T00/FOTA layout checker is missing"
    checks.append(item("T00 and FOTA static layout", layout_ok, layout_evidence,
                       "Repair all internal/HD_RES range failures; target-board QSPI and Flash evidence is still required."))

    approved = json.loads(layout_text).get("approved") is True
    checks.append(item("flash layout approved", approved,
                       "common/boot/layout.json approved=true" if approved else
                       "common/boot/layout.json is a candidate (approved=false)",
                       "Freeze the layout (Q06): measured program unit, HSE tail, D-Flash txn units, OEM sign-off."))

    files = (profile_path, linker, startup, clock, fota, probe, pbl_port,
             pbl_linker, pbl_startup, can_cfg, bsp, board_port, layout_json, c0_mem, pbl_gen)
    if layout_gate.is_file():
        files += (layout_gate,)
    return {
        "status": "STATIC_GATES_ONLY",
        "app_root": str(app),
        "pbl_root": str(PBL),
        "ready_for_mcu_flash": False,
        "failed_gates": sum(not entry["passed"] for entry in checks),
        "checks": checks,
        "source_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in files},
        "external_evidence_required": [
            "exact S32K338 order code/revision and board I/O/CAN wiring",
            "HSE AB_SWAP install and physical bank/protection mapping on a target",
            "OEM boot/security/diagnostic contract and key provisioning",
            "target-board reset, rollback, interrupted-write and recovery tests",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-root", type=Path, default=DEFAULT_APP)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    result = audit(args.app_root)
    body = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(body, encoding="utf-8")
    if args.quiet:
        print(f"PBL production source gates: {len(result['checks']) - result['failed_gates']} "
              f"passed, {result['failed_gates']} failed")
    else:
        print(body, end="")
    return 0 if result["failed_gates"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
