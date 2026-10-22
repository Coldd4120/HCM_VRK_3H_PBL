# S1：台架 PBL 按 DCMSTAT 识别 bank（PBL_BENCH_DCM_BANK）修复说明

日期：2026-10-22　基线：PBL `852e9cc`　授权：用户台架范围功能安全签字（FS-06 台架），非量产签字

## 问题
台架 PBL（`PBL_BENCH_BRINGUP`）固定报告 bank 0，并合成 `confirmed_bank=0` 的 CONFIRMED 记录。15:36 实测换到 B（DCMSTAT `0x00030FF1`）后能启动，但这是因为 PBL 把 B 当成 A 来报告：bank 没有真实来源，DCM 异常时也不会停。

## 改动（新开关 `PBL_BENCH_DCM_BANK`，默认 OFF）
- 开关约束：
  - CMake `option(PBL_BENCH_DCM_BANK OFF)`，只允许在 `PBL_BENCH_BRINGUP` + `PBL_BOARD=s32k338` 下打开，且不能与 `PBL_PRODUCTION` 同开，否则 FATAL_ERROR。
  - 源码里有 `#error` 兜底。
- 构建与追溯：
  - `tools/build.ps1 -Bench -BenchDcmBank`，输出目录后缀 `_dcm`。
  - `build_config.json` 新增 `bench_dcm_bank` 字段。
  - `PBL_VERSION` 改为 `0x0003B001`（DID FD02 可读）。
- 新增 `include/pbl_bench_bank.h`（纯函数）：
  - `pbl_bench_dcm_bank`：调用 MAIN 共用的 `Vrk_S32k338Bank_Decode(dcm, 1, …)`，要求 DONE、OTA_SCAN_OK、OTA_ACTIVE 三位同时置位，bit17 即 bank。解码失败返回 false，不会回落到 0。
  - `pbl_bench_confirmed_record`：合成的 CONFIRMED 记录中，`confirmed_bank` 和 `target_bank` 都取实测 bank。
- `pbl_board_s32k338.c`：
  - `board_physical_bank` 改为读一次 DCMSTAT（`0x402AC000`，只读）并解码。
  - `board_read_record` 跟随实测 bank；解码失败时 `pbl_run` 进恢复。
- 不改的部分：
  - 不写日志、不开 txn、不调用 HSE、不换区、不复位、不改 layout。
  - `swap_and_reset` 只在 TRIAL/ROLLING_BACK 状态下可达，而合成记录永远是 CONFIRMED，所以台架 PBL 仍然不会主动换区。

## 验证
- 主机测试 `bench_bank` 套件：
  - 用实测值验证解码：A 侧 `0x00010FF1`→bank 0，B 侧 `0x00030FF1`→bank 1。
  - 6 个无效值全部拒绝。
  - 模拟 A→B→A 三次启动都跳 C0，且没有换区、复位或写记录。
  - 用改前的记录模型（`confirmed_bank=0`）在 B 侧运行会进恢复，复现了 S1 要修的问题。
  - 解码失败时进恢复。
  - 新增 2 个变异体，均被杀死。
- 全量主机测试：ALL PBL HOST TESTS PASSED（8 suites, 23 mutants killed）。
- 默认等价：开关 OFF 时，台架 PBL 用 Arm GNU 14.2 rel1 重建，`pbl.hex` SHA256 为 `A4D1E3C84E747EB1A4540587B8AFA98F091C35E03DE0708FC33F680155C2ABBF`，与 V0.3.17/V0.3.18 包内 PBL 逐字节相同。
- 开关 ON：`pbl.hex` SHA256 为 `B77E7979C6BE05EFC3FB28475228897ED7D2ED5A9A2419464538626A0A5841B5`，bin 12848 B（OFF 时为 12728 B）。
- 与 OFF 版相比，符号层面只多了这些：`Vrk_S32k338Bank_ReadStatus`（只读 `0x402AC000`）、`Vrk_S32k338Bank_Decode`，以及 `bench_bank`@`0x2042000C` 和 `bench_bank_valid`@`0x20420008`（PBL SRAM）。

## 上板判据（S1）
1. 在 A 侧冷上电：三核 `ready_mask=7`，心跳持续增长，DID FD02 读到 `0x0003B001`。
2. 用现有探针换到 B（会写 HSE 持久状态，需单独审批），然后冷上电：
   - DCMSTAT 为 `0x00030FF1`；
   - PBL 按 bank 1 启动，三核健康；
   - 断电 ≥5 s 后仍在 B。
3. 用探针换回 A，冷上电：DCMSTAT 为 `0x00010FF1`。
4. 两侧 UTEST 前后读回不变。

回退：V0.3.18 FULL（其中 PBL 为 `A4D1E3C8…ABBF`）。