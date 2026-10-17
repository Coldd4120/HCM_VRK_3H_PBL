# HCM_VRK_3H 固定 PBL 工程

状态：**工程骨架与主机状态机已完成；板级端口未绑定，生成的 HEX 不可上板或用于量产。**

以现有三核工程为板级来源的逐项审计见 [板级接口继承与量产缺口](docs/板级接口继承与量产缺口_20261017.md)。当前板级输入在 [board_profile.json](config/board_profile.json) 中记录：S32K338、FlexCAN0、测试版 OKX0210，UJA1169 待另行适配。运行 `python tools/audit_production_readiness.py --out build/production_readiness.json` 会对不满足的量产门禁返回非零；当前 9 项静态门禁中 7 项未通过。现有 `pbl.hex` 仍是离线核心候选。

本工程是 S32K338 的独立 C0 PBL。目标架构为 HSE AB_SWAP 整半区交换、两份字节一致的固定 PBL、SBL 与三核 APP 作为同一升级包。当前链接地址 `0x00400000`，候选保护窗口 `0x00400000–0x0043FFFF`；交换侧对应 `0x00800000–0x0083FFFF`。这些地址仍待 HSE 首装后实测及 OEM 批准。

## 已实现

- C0 启动头、向量、Reset_Handler 和 256 KiB 链接预算。
- 带 CRC、版本、序号和两个副本选择的启动记录数据结构。
- CONFIRMED、ACTIVATING、TRIAL、ROLLING_BACK 状态机；试运行最多三次，失败时请求回退。
- 对回调、记录、映射、镜像验证和入口地址失败关闭；主机测试覆盖正常启动、激活、回退、损坏记录和持久写失败。
- 未绑定板级端口会拒绝启动 APP；定义 PBL_PRODUCTION 时编译直接失败。

## 尚需实现和验证

1. 根据选定 HSE 2.55.0 的 S32K338 安装实例，确认真实交换粒度、映射、HSE 预留区、IVT 和两份 PBL 保护；更新链接及入口常量。
2. 用实际驱动实现 include/pbl_core.h 中的 PblPort：读取 HSE 当前物理银行、持久记录双区读/追加、完整 SBL+C0+C1+C2 包认证、HSE 激活/回退、C0 交权、受控复位和恢复入口。
3. 设计单一持久记录的首装初始化、掉电写入语义、跨交换地址稳定性与 APP 确认接口。CRC 只检出记录损坏，不能代替认证。
4. 完成安全 I/O、看门狗、时钟、CAN UDS 最小恢复、RAM Flash Driver 及严格地址白名单。旧 APP 工程必须迁移启动头、重链并消除旧激活路径。
5. 板上验证空片首装、两次交换、两个银行的 PBL 字节一致性、保护负例、每个掉电点和三核试运行；同时满足 T00 外部 QSPI 双银行安全基线。

## 构建与验证

在本目录执行：

    powershell -ExecutionPolicy Bypass -File tools/build.ps1
    powershell -ExecutionPolicy Bypass -File tools/run_host_tests.ps1

构建产生 build/pbl.elf、build/pbl.hex、build/pbl.bin 和 build/pbl.map。`pbl.bin` 可用于离线格式检查，**不是可烧写版本**。工程目前使用独立 ARM GCC/CMake 构建；S32DS/RTD 板级集成待器件资料与板卡配置确定后完成。

设计依据见 [PBL 独立工程方案](../HCM_VRK_3H/docs/无感刷写Boot_PBL独立工程方案_20261017.md)。工厂首装候选与只读盘点见 [Factory_AllInOne](../HCM_VRK_3H_Factory_AllInOne/README.md)。
