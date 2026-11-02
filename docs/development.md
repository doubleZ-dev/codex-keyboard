# 固件开发与验证范围

[返回项目首页](../README.md) · [刷写说明](flashing.md) · [使用说明](usage.md)

## 当前版本

- 源码：[`firmware/codex-micro`](../firmware/codex-micro/README.md)，`PROJECT_VER` 为 `1.0.0-rc.7`。
- 发布包：[`dev-f9b32206ae2b`](../firmware/releases/dev-f9b32206ae2b/README.md)。
- 构建环境：ESP-IDF **5.5.3**，目标 ESP32-S3，N16R8。
- 核心依赖与版本信息见 [`dependencies.lock`](../firmware/codex-micro/dependencies.lock)。

## 模块分工

| 目录 | 作用 |
| --- | --- |
| `main/` | 主循环、设备协议、事件分发与功能整合 |
| `components/board_io/` | 物理输入与灯光输出 |
| `components/fw_foundation/` | 板级引脚、去抖、摇杆过滤等基础逻辑 |
| `components/device_services/` | 图层、动作、宏、摇杆和灯效 |
| `components/config_store/` | 配置解析与持久化 |
| `components/ble_transport/` | 蓝牙连接、绑定与 HID 传输 |
| `components/usb_rpc/` | USB 与维护协议 |
| `components/usb_microphone/` | I2S 采集与 USB 麦克风 |
| `components/firmware_update/` | 更新、试启动与回滚 |
| `tests/` | 宿主侧测试、模拟接口和测试配置 |

## 从源码构建

先安装并进入 ESP-IDF 5.5.3 的已配置终端，确认 Python、CMake、Ninja 和 ESP32-S3 工具链可用。在仓库中执行：

```powershell
cd firmware/codex-micro
idf.py build
```

源码自带 `sdkconfig` 与 `sdkconfig.defaults`。如果更换芯片、分区或 PSRAM 模式，应重新检查所有相关配置，不能把原刷写包的 manifest 用于另一个硬件。

Windows 含中文的路径可能影响部分工具链。可以把仓库放在纯英文路径构建，或在仓库根目录使用现有脚本：

```powershell
python tools/firmware/build.py --idf-path C:/Espressif/esp-idf-v5.5.3 --idf-python C:/Espressif/python-env/Scripts/python.exe
```

这里的两个路径是示例，必须替换为本机环境。`build.py` 还约定了特定工具目录版本，如不匹配请使用正确的 ESP-IDF 终端和 `idf.py build`，不要把作者的目录直接照搬到本机。

## 测试与开发脚本

仓库根目录提供：

```powershell
python tools/firmware/test_upgrade.py
python tools/firmware/test_foundation.py --zig C:/tools/zig/zig.exe --idf-path C:/Espressif/esp-idf-v5.5.3
```

第二条需要真实安装的 Zig 与 IDF 文件。官方 Input SDK 合同测试还需要本机安装的 SDK JS 路径，不能把它当成无外部依赖的测试。脚本支持的参数见各文件命令行帮助。

已有刷写包中的 `host-tests.json` 是打包时的历史结果，不表示下载者的新修改已经通过测试。源码完整构建、软件测试和实机回归是不同检查。

## 当前证据能说明什么

| 证据 | 范围 |
| --- | --- |
| 作者制作视频 | 结构、焊接、打印、装配和若干功能演示 |
| `manifest.json` | 打包版本、四镜像哈希，以及既有运行安装/配置保留摘要 |
| `host-tests.json` | 当次宿主侧测试结果 |
| `official-sdk-contract.json` | 包内记录的 SDK 合同测试摘要 |
| 当前文档整理检查 | 图片、文件路径、成本复算和固件包校验 |

manifest 中的 `full_hardware_acceptance` 为 `false`。本次文档更新没有连接设备复测，因此不将演示视频扩展为“所有主机、所有功能已全部验证”。BLE 冷启动/重连、全状态灯和完整主机兼容性仍应按具体环境验收。

## 分区与配置

源码 [`partitions.csv`](../firmware/codex-micro/partitions.csv) 定义 NVS、双应用槽、OTA 数据和配置区。当前应用槽各 3 MB，配置区起始 `0x620000`、大小 `0x80000`。旧布局迁移需要完整刷写，详见[安装说明](flashing.md)。

源码 README 保留了较完整的功能和历史说明，其中某些本机路径和内部证据目录没有随仓库发布。公开制作与使用路径以 `docs/` 中的说明为入口，历史记录不作为可直接照抄的本机命令。
