# HCM_VRK_3H 固定 PBL 工程

状态：**第三阶段（Q08/Q09）S32K338 板级移植代码已就绪（候选），在若干失败关闭 TODO 解除前上板只会停在恢复模式、不会启动 C0；生成的 HEX 不可上板或用于量产。**

以现有三核工程为板级来源的逐项审计见 [板级接口继承与量产缺口](docs/板级接口继承与量产缺口_20261017.md)。当前板级输入记录在 [board_profile.json](config/board_profile.json) 中：S32K338、FlexCAN0、测试版 OKX0210，UJA1169 待另行适配。运行 `python tools/audit_production_readiness.py --out build/production_readiness.json` 时，若量产门禁不满足会返回非零；当前 10 项静态门禁中 5 项未通过（见第三阶段报告）。

本工程是 S32K338 的独立 C0 PBL。目标架构为 HSE AB_SWAP 整半区交换、两份字节一致的固定 PBL、SBL 与三核 APP 作为同一升级包。**地址不在本工程手写**：Flash/RAM 窗口、向量偏移、C0 窗口与 vector_align、试运行上限均由主工程 `common/boot/layout.json`（候选 `VRK_S32K338_ABSWAP_CAND1`，`approved=false`）经 `tools/layout/gen_layout.py --pbl-root` 生成到 `include/gen/pbl_layout.h` 与 `linker/gen/pbl_layout.ld`；主工程 `tools/check_fota_layout.py` 的 R5/R9 规则检查生成文件是否最新、源码中有无字面地址。

## 已实现

- 启动（`startup/pbl_startup.S`）：SBAF IVT（CM7_0 起始地址 = 生成的 PBL 向量地址），16+233=249 项完整向量表（NUMBER_OF_INT_VECTORS）（每项指向 Default_Handler），设置 MSP/VTOR、使能 TCM，对 PBL DTCM 和 PBL SRAM 两个窗口做 64 位写 ECC 初始化，拷贝 .data/.ramfunc。
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

    powershell -ExecutionPolicy Bypass -File tools/build.ps1
    python tools/run_host_tests.py

S32K338 板级构建请另建构建目录，加上 `-DPBL_BOARD=s32k338`（RTD 不在默认路径时再加 `-DPBL_RTD_HEADER_DIR=<RTD header>`）。主机测试包含状态机、向量门、UDS/ISO-TP/白名单，以及 18 个变异测试，每个变异都必须被测试捕获。

设计依据见 [PBL 独立工程方案](../HCM_VRK_3H/docs/无感刷写Boot_PBL独立工程方案_20261017.md)与主工程 `docs/M0_第三阶段报告_Q08-Q10_20261017.md`。工厂首装候选与只读盘点见 [Factory_AllInOne](../HCM_VRK_3H_Factory_AllInOne/README.md)。
