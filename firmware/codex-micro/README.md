# Codex Micro 固件工作副本

更新：2026-10-30；源码版本 **1.0.0-rc.7**（BLE 分报告就绪及双击 FAST 累积灯效候选）；ESP32-S3-WROOM-1-N16R8 / ESP-IDF 5.5.3。
这是完整板软件候选版，`CONFIG_CODEX_MAINBOARD_BRINGUP=n`。板上已在线安装并确认1.0.0-rc.7，原配置逐字节一致、三个BLE绑定保留。已通过部分实体输入/高级配置/状态灯和电压测量，整机与所有主机兼容验收仍以证据记录为准。

## 当前实现

- H1固定GPIO：13键矩阵、编码器及按压、摇杆ADC及按压、Touch V2、13颗SK6812 RGB、8颗WS2812 RGB、3颗独立状态灯。没有更改板级接线。
- 保留已验证Vendor Report6和TinyUSB CDC；标准键盘Report1、Consumer Report2、System Control Report3使用原有第二HID端点，未增加USB端点。
- `v.oai.hid/rad/thstatus/rgbcfg`、`device.status`和配置服务。未知电量百分比及充电字段缺省，不返回null。
- 六个Profile，每个六层；普通KC、KV_OAI、宏KA_A、多击KA_M、Smart Actions SA、已确认语义的KI内部键。Profile和公开层位置按Input数组顺序，`device.status.layer_index`为1起算，`kb.radial`和`kb.cs.*`为0起算。
- VENDOR摇杆沿用原通路。RADIAL解析官方Input默认扇区、环绕和间隙，拒绝重叠；预览/激活/保持/释放/关闭通知接入实际主循环。
- 宏最多32项、共512步、4并发，每实例最多16种同时持有的键，单步延时至10秒、单宏至60秒。act0释放、1按下、2点击；delay为步骤完成后的等待。宏不递归。
- 多击最多32项：单击、双击、长按、单击后长按，tt为1..60000ms；共享ID按引用计数最后释放，支持宏分支，不支持多击递归。
- Smart Actions最多64项：TEXT_STEP/text、CMD_STEP/cmd、APP_STEP/path（name可选）、URL_STEP/url；单字段最多4096 UTF-8字节，转义后的通知也须满足6144字节上限。主机Input负责执行和权限，不在MCU执行命令。
- AppSense最多32个应用身份，Host提供process/path；非空字段均须匹配。Windows风格身份处理ASCII大小写和斜杠差异；其他路径保持精确匹配。优先当前Profile，再按配置顺序，未知焦点不切换，相同焦点不覆盖手动选择。
- BLE HOGP，三个独立Bond槽，每次连接一个主机；选择/配对/忘记、身份隔离、加密/订阅门槛、断线重连、持久删除意图。瞬时安全启动ENOMEM/EBUSY最多5次重试、间隔250ms，认证总截止15秒，不无限等待。
- 两文件NVS双bank原子提交、CRC和回读校验、旧schema1/2读入、schema3写入、损坏回退及未来schema只读保护。双应用槽升级、试启动确认和120秒回滚、ROM恢复。

## 输入与层切换

默认矩阵和Agent映射保留；启动已按住的开关抑制到释放，不伪造初始按下。输出拥塞时保留队首，不能丢释放。普通切层释放旧物理绑定，但保留临时层键/多击捕获的释放绑定，正在执行的宏继续使用其固定步骤和按下时Agent。Profile/AppSense变更、事件丢失执行完整清理；断线清空会话并在新主机先发送中立报告。

`KI_LS1..6`永久选择、`KI_LM1..6`临时选择，最近按下的临时层优先，最后释放后恢复永久层；`KI_PS1..6`选择Profile。未配置的目标不切换。`KI_CS_SHOW/SHOW_TMP/HIDE/TOGGLE`发送已确认的`kb.cs.*`，临时显示在释放时隐藏。`KI_BLUP/BLDW`调节RAM亮度比例0..1；`KI_CBT1..3/CBTP1..3/CUSB`明确选择传输/配对。`KI_X`关闭摇杆面板，回中后才可重新打开。

官方Input中`KI_FP`只有“FP function keycode”标签，设备语义没有证据，因此提交时明确拒绝；不能用空实现或无限重试冒充兼容。设备原生应用安装/屏幕等非本PCB需求不作为已支持功能。

摇杆保持0.8.1已验证过滤：3点中值、64个稳定样本启动中心、Q8 EMA alpha1/4、进入240/退出160 ADC counts迟滞。启动需松手至少2秒；校准只在RAM，不在运行中追随拨动重学。RADIAL使用归一化距离0.10回中关闭、0.80激活/0.60释放迟滞，角度0为+X、0.25为+Y；这些是本项目执行策略，实际板方向/行程须回归。每40ms最多一帧面板通知，避免Host三秒自动关闭。

## 灯光和电池

层`lights.backlight/underglow`使用官方Input DTO：effect为off/solid/snake/rainbow/breath/gradient，brightness/speed/magic为0..1，color为可选24位整数RGB。RADIAL层使用自己的灯光，不叠加Codex Agent；VENDOR层允许Agent同步覆盖。灯效曲线为项目实现，不宣称与原厂逐像素相同。亮度比例和驱动64/255上限共同限制输出；低电压仅提示，不额外调暗。这是软件限亮，不是测量电流或硬件过流保护。

`diag.lights.status`只读返回Host指令时间、效果/代数、亮度比例、RMT故障及最近8个实际提交帧，包含两条RGB链所有像素；用于定位命令行灯偶发闪动，尚未证明其实际原因。RMT TX ISR/复制编码器启用cache-safe，Flash写入时不依赖被禁用的指令缓存。

电池ADC按H1分压还原电压；低于3.5V连续3次采样置低电，高于3.65V解除，小于2.5V不作为有效低电判断。单路ADC不能可靠识别电池在位或充电，未知百分比/充电字段缺省。电池实测仍按用户要求暂缓。低功耗、软件休眠、节能唤醒已取消，机械滑动开关不由软件模拟。

## USB、BLE及麦克风

USB：Vendor IN81、标准HID IN84、CDC IN83/OUT03、UAC2音频IN82；含EP0共5个IN，配置250B。原Vendor报告29B不变，CDC可选通知端点省略。INMP441 SCK12/SD13/WS14、L/R接GND；I2S0 Philips stereo32/48kHz取左声道，USB mono PCM16。USB录音与配置/升级原子互斥，不使用电脑其他麦克风、不自动改默认设备，不提供BLE音频。

显式切换先等待BLE命令token成功且目标槽一致，再释放旧Host按键/宏/摇杆、接纳三个标准中立报告并排空旧Vendor队列；期间冻结新输入，失败不抢占错误主机。

默认USB连接优先，拔掉后用当前BLE；内部CBT/CBTP明确选择BLE可在USB供电时使用BLE输入，CUSB切回USB。每次切换新会话先中立释放。广播Codex Micro #1/#2/#3；空槽配对窗口60秒。短触摸切下一配置层，长触摸2秒切下一BLE槽，空槽开放配对。长按释放不重复切层。

`diag.ble`无op只查询；op为select/pair/forget、slot0..2。accepted_command只表示入队，须等last_command匹配并检查error、paired_mask、ready/vendor_ready/MTU。HOGP键盘/Consumer须加密并订阅；Vendor另需MTU>=66及Report6订阅。System Control订阅独立；主机不支持的输出会计入diag.transport.unavailable_outputs，不阻塞后续释放。GATT变化发送Service Changed，保留Bond。Just Works不包含人工数字MITM确认。

旧版实测：三Bond保存与重启保留通过，iPhone/安卓订阅就绪；Windows服务就绪历史失败。候选加入有界安全重试及GATT缓存通知，仍须真实Windows重连验证，不能提前声称问题根治。

## 配置和内存

keymap.json上限131072字节，smart_actions.json上限32768字节；fs.list返回已提交内容的SHA1，readbin最多3072原始字节，writebin核对offset/tx/append/completed。txcommit完成Flash回读才响应；拒绝提交仅返回error，运行时和读回继续为旧配置。事务会话变化或30秒无活动丢弃暂存，已接纳的Flash作业可完成，但不给新会话发送旧ACK。单次JSON-RPC上限6144字节；配置JSON深度上限32，HID流帧深度16。

大配置原文、模型和暂存区位于模块内8MB octal PSRAM；cJSON优先PSRAM。RTOS队列/任务栈及RMT符号保持内部RAM，预留32KB内部堆。协议任务独占业务、层和配置状态，Flash worker只接受复制后的候选，NimBLE Host独占连接/Bond。协议栈12288B、CDC栈6144B；其他任务维持原配置。

## 升级、构建与测试

布局3：保留NVS0x9000、ota_0 0x10000/3MB、ota_1 0x310000/3MB、otadata0x610000；config在0x620000扩展至512KB。升级ABI1/config schema3。从0.8.1首次迁移必须ROM完整安装四镜像，保留NVS/config/Bond；不能以旧OTA越过分区表变化。后续同布局在线升级可用CDC，旧HID维护接口仍保留。

CDC维护：项目私有CMUP v1、LE长度、CRC32、COBS零分隔，JSON上限2048B。串口独占，日志只在完整控制帧之间输出；与官方HID帧分离，同一升级状态机写入。upgrade.py --transport auto先只读握手CDC，旧固件回退HID；--transport cdc失败不回退。超时查询持久offset，只允许确认未提交后的同块一次重发。原HID多应用交错的确切根因尚无完整抓包，不冒称已修复。

在工作区根目录运行：

```powershell
python tools/firmware/test_foundation.py
python tools/firmware/test_upgrade.py
node tools/firmware/test_official_input.cjs C:/Users/zengzhi/AppData/Local/Programs/input/resources/app.asar.unpacked/node_modules/@worklouder/wl-device-kit/dist/index.js
python tools/firmware/build.py --incremental --idf-path G:/ESP32/archive_vv5.5.3_windows-x64/v5.5.3/esp-idf --idf-python C:/Users/zengzhi/.espressif/python_env/idf5.5_py3.13_env/Scripts/python.exe
python tools/firmware/package.py
```

软件测试包括真实C矩阵/消抖、摇杆、USB回调/分帧、HID引用、宏并发取消、多击四分支、Smart四类型、AppSense、实际内部键分派、RADIAL、BLE SDK替身/NVS故障、真实config_rpc及SDK Base64/SHA1、升级引擎/工具、CDC两语言交叉校验。安装版官方SDK已通过44.8KB六Profile×六层配置写入、读回及非法扇区回退；这是SDK+C服务合同测试，Flash/链路为宿主边界，不能替代官方GUI和设备测试。正式运行结果见docs/firmware/evidence和Firmware_Execution_Log.md。

## rc.2 电池低电量提醒

固定H1 100k/100k分压与100nF，校准ADC×2。250ms采样、5点中值、连续8次<=3.50V告警、连续8次>=3.65V恢复。三状态灯1Hz同闪；不额外调暗RGB，不休眠、不自动关机、不推断充电/百分比。项目私有diag.battery.warning_test只演示10秒提醒，不修改ADC或真实低电量标志。正式契约与实测边界见docs/firmware三文档及电池证据目录。

## rc.3 BLE输出与默认RGB修复

Output报告补齐加密READ及合法值读回，Windows64字节写入和BLE关联RPC已实测通过；RGB蓝牙绿/蓝区域控制及显式关灯由实际帧和用户确认。未收到当前Host配置时项目默认白色常亮，Host明确off及层配置仍优先。原配置和三个绑定保留，测试切换键已撤销。详细证据见candidate-1.0.0-rc.3/Fix_Review.md。

## rc.4 Codex旋钮转动修复

当前官方客户端仅接受ENC_CW/ENC_CC的act=2。Vendor旋转现在按每格发送act=2，宏/重映射的释放不再产生额外一格；普通KC、按键及旋钮按压仍保留原行为。实际序列化/队列回归、完整C回归、官方SDK74次配置调用及IDF构建通过；已在线安装、精确镜像确认，实体菜单回归结果另见candidate-1.0.0-rc.4。宽键录音、转写和独立Codex键发送在此前rc.3用户实测通过，未为此改变映射。曾偶发的Agent切回窗口焦点问题最后未复现，不宣称已修复。

## rc.5 用户指定灯效演示

第三排最左侧物理Fast键（矩阵位8）保留为本地演示开关，按下切换启动/退出，按下和松开均不再向Host转发。此本地覆盖独立于Input层配置，原配置文件不重写；diag.lights.status明确公开demo_active、demo_phase、demo_step_ms和demo_trigger。开机按住不自动触发；仅RAM保存演示状态，升级自动停止，可在无Host连接时触发。

rc.6调整为每段2秒、22秒循环：六Agent混合色/底灯关2秒，随后依次白(Agent3)、蓝(Agent2)、绿(Agent0)、琥珀黄(Agent1)、红(Agent5)，每色先底灯流水2秒，再8颗底灯全部同色常亮2秒；对应Agent键在这4秒内保持点亮。混合色为绿/琥珀黄/蓝/白/蓝/红；其他7颗按键灯关闭。颜色从用户单色截图选中状态实心圆精确提取：FFFFFF/3079FF/4CD475/FFCC00/FF3D81，坐标和源文件哈希见candidate-1.0.0-rc.6/palette-samples.json；不冒充真实Host任务状态。三状态灯及低电提示不被覆盖；RGB仍受64/255驱动上限。诊断灯测试优先于演示，演示优先于Host/层RGB；Host设置持续更新，退出立即恢复最新效果。

## rc.7 本轮变更

BLE 每种报告独立就绪，Vendor 不再依赖键盘和媒体键订阅；缺少标准报告订阅时跳过对应中立报告并记录不可用输出，真实队列拥塞仍重试。diag.ble 暴露授权、加密、四类就绪及订阅位图。用户反馈 rc.6 纯电池冷启动后 Agent 双击失效：上述耦合已由真实 C 回调测试复现，尚须修复版冷启动实测确认是否为现场唯一原因。

双击 FAST（350ms 内）新增五键累积演示：第一排白、蓝，第二排绿、琥珀、红，从第一颗立即亮起，每隔1秒加一颗，4秒后五颗全亮并保持；第六颗 Agent 及底灯关闭。单击保留原22秒演示，再单击退出（等待350ms以区分双击）。颜色沿用 rc.6 截图取色。两种演示均不改持久配置，三状态灯继续显示连接/低电状态。
