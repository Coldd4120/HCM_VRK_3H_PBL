# HCM_VRK_3H 固定 PBL 工程

2026-10-22 HOST_DEV更新：`tools/build.ps1 -Board s32k338 -TxnBackend -FactoryInstall -HseInstallSource <候选地址> -BuildDir <F盘会话目录>`可生成独立工厂IVT候选，禁止与bench/production组合。构建身份记录安装源及未验证状态；普通运行版不变。真实逐银行安装规则未批准，不将该候选直接烧录。FW_PARTITION错误银行候选已移除，后续绑定真实BootMap。详见 [实施记录](../HCM_VRK_3H/docs/实施记录/换区激活/20261022_HSE替代输入推进与软件实施记录.md)。

状态：**第三阶段（Q08/Q09）S32K338 板级移植代码已就绪（候选），在若干失败关闭 TODO 解除前上板只会停在恢复模式、不会启动 C0；生成的 HEX 不可上板或用于量产。**

以现有三核工程为板级来源的逐项审计见 [板级接口继承与量产缺口](docs/板级接口继承与量产缺口_20261017.md)。当前板级输入记录在 [board_profile.json](config/board_profile.json) 中：S32K338、FlexCAN0、测试版 OKX0210，UJA1169 待另行适配。运行 `python tools/audit_production_readiness.py --out build/production_readiness.json` 时，若量产门禁不满足会返回非零；当前 10 项静态门禁中 5 项未通过（见第三阶段报告）。

本工程是 S32K338 的独立 C0 PBL。目标架构为 HSE AB_SWAP 整半区交换、两份字节一致的固定 PBL、SBL 与三核 APP 作为同一升级包。**地址不在本工程手写**：Flash/RAM 窗口、向量偏移、C0 窗口与 vector_align、试运行上限均由主工程 `common/boot/layout.json`（候选 `VRK_S32K338_ABSWAP_CAND1`，`approved=false`）经 `tools/layout/gen_layout.py --pbl-root` 生成到 `include/gen/pbl_layout.h` 与 `linker/gen/pbl_layout.ld`；主工程 `tools/check_fota_layout.py` 的 R5/R9 规则检查生成文件是否最新、源码中有无字面地址。

## 已实现

- 启动（`startup/pbl_startup.S`）：SBAF IVT（CM7_0 起始地址 = 生成的 PBL 向量地址），16+242=258 项完整向量表（与 C0 镜像的 0x408 字节表同长；RTD 的 NUMBER_OF_INT_VECTORS 249 偏小），每项指向 Default_Handler，设置 MSP/VTOR、使能 TCM，对 PBL DTCM 和 PBL SRAM 两个窗口做 64 位写 ECC 初始化，拷贝 .data/.ramfunc。
- RAM：PBL 只使用 `pbl_sram` 0x20420000–0x2042FFFF（C0 非缓存 SRAM，C0 每次复位都会重新初始化，且位于 `__STANDBY_RAM_LIMIT_END` 0x2040FFFF 之外）和 `pbl_dtcm` 0x20000000–0x2000FFFF（栈），不碰 C0 待机保持区 0x20400000–0x2040FFFF。
- 状态机：CONFIRMED、ACTIVATING、TRIAL、ROLLING_BACK。ACTIVATING 落在目标银行时只写一次 TRIAL(a=1)；试运行上限取自 layout（候选值 3）。trusted_vector 只接受 C0 窗口内、按 vector_align 对齐的向量，并校验 MSP 和 Thumb 复位入口。
- 恢复 UDS 子集（`src/pbl_uds.c`）：0x10/0x3E/0x22/0x11；下载类服务 0x31/0x34/0x36/0x37 返回 NRC 0x22（待 Q07 描述符与认证完成）。同时提供 ISO-TP 单帧和编程地址白名单（`src/pbl_flash_policy.c`，仅允许被动半区 descriptor..C2）。
- S32K338 板级（`-DPBL_BOARD=s32k338`，`src/board/s32k338/`）：HSS 安全输出（同 Vrk_BootForceHssOff）、SWT、MPU 默认、FlexCAN0+TJA1044 STB、C40 RAM 驱动、HSE MU 最小服务（GET_ATTR/激活被动半区）、跳转 C0（关中断、清 NVIC pending、关 SysTick/MPU、设 VTOR/MSP）。寄存器取自 S32DS 安装目录下 RTD 6.0.0 的 S32K358 头文件（经 `PBL_RTD_HEADER_DIR` 引用，不拷贝进仓库）。
- `PBL_BOARD=unbound`（默认）仍是失败关闭的空端口；`PBL_PRODUCTION` 在布局未批准或板级仍有 TODO 时编译失败。

## 失败关闭 TODO（解除前不启动 C0）

1. 物理银行：HSE 中可用作 A/B 银行来源的属性尚未在目标板上验证（`PBL_HSE_BANK_FROM_FW_PARTITION_VERIFIED`）。
2. 启动记录：layout 中 D-Flash 事务单元偏移仍为 null（Q07）。
3. 包认证：描述符格式加 HSE SMR 验证（Q07）。
4. CAN：FLEXCANA 时钟树（FXOSC + MC_CGM）未配置（`PBL_CAN_CLOCK_VERIFIED`），恢复模式下暂不收发，只喂狗。
5. C40：域 ID 与扇区锁映射（`PBL_C40_DOMAIN_ID`）。

## 构建与验证

在本目录执行：

    # 失败关闭的空端口（默认）
    powershell -ExecutionPolicy Bypass -File tools/build.ps1
    # S32K338 板级；台架首板直接跳主份 C0
    powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Board s32k338 -Bench
    python tools/run_host_tests.py

`-Bench` 对应 `PBL_BENCH_BRINGUP`，`-TxnBackend` 对应 `PBL_TXN_BACKEND`；两者都是真实编译开关，必须显式传入，不能再靠手改 `CMakeCache.txt`。`-Bench` 只允许与 `-Board s32k338` 组合。RTD 不在默认路径时加 `-DPBL_RTD_HEADER_DIR=<RTD header>`。

每次构建都会写 `<build>/build_config.json`（board / bench / txn / production / layout 名称与哈希 / pbl.hex、pbl.bin 的 SHA256）。工厂打包器要求它与实际的 `pbl.bin` 一致，并拒绝 `board=unbound` 或无启动路径（`bench_bringup` 与 `production` 都为假）的 PBL——那种镜像永远停在失败关闭的恢复桩，不会启动 C0。

主机测试包含状态机、向量门、UDS/ISO-TP/白名单，以及 21 个变异测试，每个变异都必须被测试捕获。C0 向量表长度按真实镜像取 258 项（0x408 字节，Reset 在 +0x420）：RTD 的 `NUMBER_OF_INT_VECTORS`（249）偏小，用它会把表内地址当成合法入口。

## 台架启动前置条件

- PBL 在任何 MCAL 驱动之前自己请求 SIUL2 时钟（PRTN1 COFB1 REQ42），不再依赖复位默认；读回失败时安全输出检查失败，PBL 停在恢复循环。
- 恢复模式下 `PBL_CAN_CLOCK_VERIFIED` 未定义时 CAN0 不启动：PBL 既不跳转也不发报文，只能用调试器或复位行为判断。上板实测 FLEXCANA 时钟后再打开该开关。
- C0 侧编码脚 PTE15 必须稳定：`Vrk_Core0Bsp_InitCodingPin()` 在 1 ms 间隔上采样 3 次，任一跳变即 `E_NOT_OK`，C0 不发布 READY，C1/C2 等 2 s 后发布 ERROR，整机停在 Safe。台架需接固定电平或短接，否则会误判为“起不来”。
- C0 启动失败不再静默：`Vrk_Core0Boot_Fault`（`.standby_data`，0x20400000）在功能复位后保留失败阶段与错误码，`vrk_boot_clock.c` 会对前 3 次失败触发功能复位，之后停在断点友好的死循环里。

- 设计依据见 [PBL 独立工程方案](../HCM_VRK_3H/docs/无感刷写Boot_PBL独立工程方案_20261017.md)与主工程 `docs/M0_第三阶段报告_Q08-Q10_20261017.md`。工厂首装候选与只读盘点见 [Factory_AllInOne](../HCM_VRK_3H_Factory_AllInOne/README.md)。
