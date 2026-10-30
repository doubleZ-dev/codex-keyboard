# Codex Micro 1.0.0-rc.7 候选固件包

模式：full。本包面向 H1 / ESP32-S3-WROOM-1-N16R8 / ESP-IDF 5.5.3。
功能、协议证据、容量和实机项目见 Firmware_Details.md。候选包不代表整机验收通过。

## 安装与数据保留

1. 安装前备份 keymap.json、smart_actions.json，核对三个 BLE Bond。
2. 本版布局3/config schema3；配置分区在原0x620000扩展为512KB。
   从0.8.1及更旧布局首次迁移须完整ROM安装四镜像，不能只写应用或走旧OTA。
3. BOOT按住、短按EN、松开BOOT，重新检测实际ROM端口。
   执行 `./flash.ps1 -Port COM45`；COM45须替换为本次检测结果。
   脚本校验四镜像SHA256，不全片擦除，保留旧NVS/config及BLE绑定。
4. 正常复位，摇杆松手至少2秒。检查版本/运行ELF哈希、PSRAM、配置generation、
   输入释放/归中、BLE绑定、音频枚举和灯光诊断。

## 后续同布局在线升级

`python upgrade.py --transport auto --install firmware.bin` 先握手CDC维护通路，
旧固件可回退HID。需要本地pyserial和hidapi，关闭占用COM口的日志监视器，
停止录音、释放输入并完成配置写入。超时后须对账持久offset，禁止盲重发。
CDC项目维护帧与官方Vendor Report6分离，采用CRC32和有界COBS分帧；
仍使用同一升级写入、哈希、激活、试启动和回滚状态机。

## 验证与边界

不自动应用examples。config_apply.py不带--apply只检查本地文件；带--apply
事务写入两文件并逐字节读回。Smart Actions在Input主机执行，权限由Input控制。
USB麦克风INMP441左声道，48kHz mono PCM16，不提供BLE音频，不改电脑默认录音。
低功耗取消；电池实测暂缓。只提供电压与低电提示，不伪造百分比/在位/充电。
RGB上限64/255是软件限亮，不是电流测量。KI_FP未知语义不作为已支持功能。
官方GUI、无线重连、全键与灯光回归另存实测证据。个人录音不打入包。
