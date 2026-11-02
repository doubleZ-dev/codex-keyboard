# 刷写与首次启动

[返回项目首页](../README.md) · [使用说明](usage.md) · [开发说明](development.md)

## 先确定使用哪个包

当前包：[dev-f9b32206ae2b](../firmware/releases/dev-f9b32206ae2b/README.md)，版本 **1.0.0-rc.7**，目标 **ESP32-S3-WROOM-1-N16R8 / H1**。

新板或从 0.8.1 等旧分区布局迁移时，必须完整安装四个镜像。同布局后续升级才使用单独的应用镜像。这个包的 Flash 为 16 MB，配置 schema 为 3，不适用于随意替换的 ESP32 型号。

| 镜像 | 写入地址 |
| --- | --- |
| `bootloader.bin` | `0x0` |
| `partition-table.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |
| `ota_data_initial.bin` | `0x610000` |

地址来自包内 `manifest.json`，不能只烧 `firmware.bin` 来完成首次安装。应用镜像 SHA-256：

```text
f9b32206ae2bee4a72ac7dd67bd00a2e4532926162b71530b07a446ae3cce709
```

## Windows 首次安装

### 1. 准备工具

下载完整仓库并解压，打开 PowerShell，进入 `firmware/releases/dev-f9b32206ae2b/`。安装 Python 与 esptool 4.x：

```powershell
python -m pip install "esptool>=4.8,<5"
python -m esptool version
```

刷写包脚本使用 esptool 4.x 的参数风格，因此这里限制在 4.x。若已经使用 ESP-IDF 的 Python 环境，也可直接使用其中的 esptool。[Espressif 官方命令说明](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/esptool/basic-commands.html)

### 2. 进入下载模式

完成硬件电气检查后，用数据线连接主控板。按住 **BOOT**，短按 **EN/复位**，松开 BOOT。在设备管理器重新确认这一次出现的 COM 端口；ROM 下载模式与正常运行时的端口可能不同。

已经使用过的板，在更改固件之前先备份自己的配置。不要因为旧文档示例出现 COM45 就直接使用该端口。

### 3. 运行包内脚本

下例的 `COM7` 必须替换为设备管理器显示的实际端口：

```powershell
./flash.ps1 -Port COM7 -IdfPython (Get-Command python).Source
```

显式传入 `-IdfPython`，就不会依赖脚本中作者本机的默认路径。脚本会先核对四个镜像的 SHA-256，再按 manifest 中的地址写入；没有全片擦除步骤。

若 PowerShell 阻止执行脚本，可在确认来源后，仅对本次运行使用：

```powershell
powershell -ExecutionPolicy Bypass -File ./flash.ps1 -Port COM7 -IdfPython (Get-Command python).Source
```

### 4. 启动并检查

1. 写入成功后正常复位，松开所有按键，摇杆至少 2 秒保持松手。
2. 检查系统是否识别 USB 输入、CDC 和音频设备。
3. 打开桌面端，逐项测试 Agent 键、功能键、旋钮、摇杆与触摸。
4. 在音频输入中选择 `Codex Micro INMP441`，测试实际声音和转写。
5. 按[装配检查表](assembly.md#8-首次使用检查)完成检查后再封壳。

脚本只是不主动全片擦除，并不等于所有旧分区/固件都能安全升级。首次迁移仍需备份与核对分区；同布局升级使用下面的维护路径。

## 同布局后续升级

以下在刷写包目录运行。正常连接运行中的设备，不是 ROM 下载模式：

```powershell
python -m pip install pyserial hidapi
python backup.py --out ./my-backup
python upgrade.py --transport auto --status
python upgrade.py --transport auto --install firmware.bin
```

`my-backup` 中保存的是个人配置，放在自己的机器上，不要提交到公开仓库。`auto` 先尝试 CDC 维护握手，旧设备可回退 HID；固定 CDC 可改为 `--transport cdc`。

维护脚本按设备返回信息选择连接，不应在多个同类设备接入时盲操作。升级前结束录音、释放按键，关闭串口监视器和其它占用设备的工具，保持供电稳定。升级后确认运行版本、配置读回和蓝牙绑定，而非仅看传输进度完成。

## 常见问题

| 现象 | 检查 |
| --- | --- |
| 没有 COM 端口 | 检查数据线、USB 口、供电、BOOT/EN 时序与系统设备管理器 |
| 端口被占用 | 关闭串口日志窗口、录音和其它维护进程 |
| SHA-256 校验失败 | 重新下载完整包，不要混用不同版本镜像或手动绕过校验 |
| 完成写入但不启动 | 确认 N16R8 型号、四镜像地址、完整分区布局和实际供电 |
| 单独应用镜像升级失败 | 核对当前设备是否已经使用布局 3；旧布局先完整安装 |

本文新增的是安装文档，本次整理没有连接实物或执行烧录。包内历史测试摘要见 `manifest.json`、`host-tests.json` 和 `Firmware_Details.md`。
