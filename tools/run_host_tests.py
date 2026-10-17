"""PBL host tests: state machine, Q09 vector gate, recovery UDS subset,
ISO-TP single frame, flash whitelist, plus mutation sensitivity.

    python tools/run_host_tests.py [--cc "zig cc"|gcc]

Every mutant weakens one safety check in a copy of the production source; the
test suite must fail against each of them (Q03 style), otherwise the tests are
not sensitive to that check. Build outputs go to a temporary directory.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZIG_DEFAULT = Path(r"E:\work\VRK\01_FIRMWARE\HCM_VRK_3H_OUT\TOOLS\srs_test_deps\ziglang\zig.exe")
CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2"]

SUITES = {
    "core": (["src/pbl_core.c"], "tests/test_pbl_core.c"),
    "uds": (["src/pbl_uds.c", "src/pbl_flash_policy.c"], "tests/test_pbl_uds.c"),
    # Q07/Q13 shared transaction record, byte-identical to the main repo
    # (common/boot/txn). Compiled here to prove it builds in the PBL context.
    "txn_record": (["src/txn/txn_record.c"], "tests/test_txn_record.c"),
    "txn_store": (["src/txn/txn_record.c", "src/txn/txn_store.c"], "tests/test_txn_store.c"),
}

# (suite, file, old, new): each must make the suite fail.
# Not listed (equivalent mutant): the "(C0_END - vector) <= VECTOR_BYTES" room
# check. C0_END is vector_align aligned and VECTOR_BYTES < vector_align
# (#error in pbl_core.h), so every aligned in-window vector has room; the
# check is kept as defence in depth.
MUTANTS = {
    "vector window upper bound removed": (
        "core", "src/pbl_core.c", "vector >= PBL_LAYOUT_C0_END ||", ""),
    "vector window starts at the descriptor": (
        "core", "src/pbl_core.c", "vector < PBL_LAYOUT_C0_BASE ||",
        "vector < PBL_LAYOUT_DESCRIPTOR_BASE ||"),
    "vector alignment relaxed to 0x80": (
        "core", "src/pbl_core.c", "(vector & (PBL_LAYOUT_C0_VECTOR_ALIGN - 1u)) != 0u",
        "(vector & 0x7Fu) != 0u"),
    "MSP upper bound removed": (
        "core", "src/pbl_core.c", "msp > PBL_LAYOUT_RAM_C0_MSP_WINDOW_END", "msp > 0xFFFFFFF0u"),
    "MSP alignment removed": (
        "core", "src/pbl_core.c", "|| (msp & 7u) != 0u) {", ") {"),
    "Thumb bit not checked": (
        "core", "src/pbl_core.c", "if ((entry & 1u) == 0u) {", "if (entry == 0xFFFFFFFFu) {"),
    "reset handler may point into the table": (
        "core", "src/pbl_core.c", "entry >= (vector + PBL_C0_VECTOR_BYTES)", "entry >= vector"),
    "reset handler may leave C0": (
        "core", "src/pbl_core.c", "&& entry < PBL_LAYOUT_C0_END;", ";"),
    "ACTIVATING writes TRIAL twice": (
        "core", "src/pbl_core.c",
        "        r.trial_boots = 1u;\n        if (!persist(p, &r, PBL_STATE_TRIAL)",
        "        r.trial_boots = 0u;\n        (void)persist(p, &r, PBL_STATE_TRIAL);\n"
        "        r.trial_boots = 1u;\n        if (!persist(p, &r, PBL_STATE_TRIAL)"),
    "ACTIVATING skips the vector gate": (
        "core", "src/pbl_core.c",
        "        if (!trusted_vector(p, r.target_package_id, &vector)) {\n            return rollback(p, &r);\n        }\n        r.trial_boots = 1u;",
        "        (void)trusted_vector(p, r.target_package_id, &vector);\n        r.trial_boots = 1u;"),
    "trial limit off by one": (
        "core", "src/pbl_core.c", "r.trial_boots >= PBL_MAX_TRIAL_BOOTS", "r.trial_boots > PBL_MAX_TRIAL_BOOTS"),
    "whitelist reaches the spare area": (
        "uds", "src/pbl_flash_policy.c",
        "#define WINDOW_END   (PBL_LAYOUT_C2_PASSIVE_BASE + PBL_LAYOUT_C2_SIZE)",
        "#define WINDOW_END   (PBL_LAYOUT_SPARE_PASSIVE_BASE + PBL_LAYOUT_SPARE_SIZE)"),
    "whitelist includes the passive PBL copy": (
        "uds", "src/pbl_flash_policy.c",
        "#define WINDOW_START PBL_LAYOUT_DESCRIPTOR_PASSIVE_BASE",
        "#define WINDOW_START PBL_LAYOUT_PFLASH_PASSIVE_BASE"),
    "whitelist length overflow": (
        "uds", "src/pbl_flash_policy.c", "length <= (WINDOW_END - address)",
        "address + length <= WINDOW_END"),
    "program may cross a C40 page": (
        "uds", "src/pbl_flash_policy.c",
        "           (address / PBL_FLASH_PAGE_BYTES) ==\n               ((address + length - 1u) / PBL_FLASH_PAGE_BYTES);",
        "           1;"),
    "download service enabled": (
        "uds", "src/pbl_uds.c", "    case 0x34u:\n", ""),
    "functional NRC not suppressed": (
        "uds", "src/pbl_uds.c", "    if (functional && (code == PBL_UDS_NRC_SNS",
        "    if (!functional && false && (code == PBL_UDS_NRC_SNS"),
    "ISO-TP accepts DLC shorter than PCI": (
        "uds", "src/pbl_uds.c", " || n + 1u > dlc) {", ") {"),
    "txn sequence+1 check removed": (
        "txn_record", "src/txn/txn_record.c",
        "if (next->sequence != (uint32_t)(cur->sequence + 1u)) {", "if (0) {"),
}



def txn_drift_check() -> int:
    """The PBL copy of the shared transaction record must stay byte-identical to
    the main repo (common/boot/txn). Skipped when the main repo is not alongside."""
    import hashlib
    main = Path(os.environ.get("VRK_MAIN_ROOT", ROOT.parent / "HCM_VRK_3H"))
    pairs = [
        (ROOT / "include/txn_record.h", main / "common/boot/txn/txn_record.h"),
        (ROOT / "include/txn_store.h",  main / "common/boot/txn/txn_store.h"),
        (ROOT / "src/txn/txn_record.c", main / "common/boot/txn/txn_record.c"),
        (ROOT / "src/txn/txn_store.c",  main / "common/boot/txn/txn_store.c"),
    ]
    if not all(m.exists() for _, m in pairs):
        print("SKIP txn drift check (main repo not found)")
        return 0
    for pbl, m in pairs:
        if hashlib.sha256(pbl.read_bytes()).digest() != hashlib.sha256(m.read_bytes()).digest():
            print(f"FAIL txn drift: {pbl.name} differs from main repo")
            return 1
    print("PASS txn drift: PBL copy byte-identical to main repo")
    return 0


def compiler(spec: str | None) -> list[str]:
    if spec:
        return shlex.split(spec)
    env = os.environ.get("PBL_ZIG")
    if env:
        return [env, "cc"]
    if ZIG_DEFAULT.is_file():
        return [str(ZIG_DEFAULT), "cc"]
    return [sys.executable, "-m", "ziglang", "cc"]


def build_and_run(cc: list[str], root: Path, suite: str, exe: Path):
    sources, test = SUITES[suite]
    cmd = cc + CFLAGS + ["-I", str(root / "include")]
    cmd += [str(root / s) for s in sources] + [str(ROOT / test), "-o", str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        return build, None
    return build, subprocess.run([str(exe)], capture_output=True, text=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", help='host compiler command, default: Zig "cc"')
    args = parser.parse_args()
    cc = compiler(args.cc)
    if txn_drift_check() != 0:
        return 1
    tmp = Path(tempfile.mkdtemp(prefix="pbl_host_"))
    try:
        for suite in SUITES:
            build, result = build_and_run(cc, ROOT, suite, tmp / f"{suite}.exe")
            if result is None:
                print(build.stdout + build.stderr)
                print(f"FAIL host build: {suite}")
                return 1
            print(result.stdout.strip())
            if result.returncode != 0:
                print(f"FAIL host tests: {suite}")
                return 1
        killed = 0
        for index, (name, (suite, rel, old, new)) in enumerate(MUTANTS.items()):
            text = (ROOT / rel).read_text(encoding="utf-8")
            if text.count(old) != 1:
                print(f"FAIL mutation setup: pattern for '{name}' not unique in {rel}")
                return 1
            mroot = tmp / f"mut{index}"
            shutil.copytree(ROOT / "include", mroot / "include")
            shutil.copytree(ROOT / "src", mroot / "src")
            (mroot / rel).write_text(text.replace(old, new), encoding="utf-8")
            build, result = build_and_run(cc, mroot, suite, mroot / "mutant.exe")
            if result is None:
                print(build.stdout + build.stderr)
                print(f"FAIL mutation '{name}' did not compile")
                return 1
            if result.returncode == 0:
                print(f"FAIL mutation survived: '{name}'")
                return 1
            killed += 1
            print(f"PASS mutation killed: '{name}'")
        print(f"ALL PBL HOST TESTS PASSED ({len(SUITES)} suites, {killed} mutants killed)")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
