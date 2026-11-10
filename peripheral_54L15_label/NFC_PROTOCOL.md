# nRF54L15 NFC 配置协议说明

本文描述 SignalConvert-R4 固件当前实现的 NFC 标签协议。协议分为两层：

1. **NFC Forum Type 4 Tag / NDEF**：手机与芯片交换 NDEF 内容的标准机制，由 Nordic Type 4 Tag 库处理。
2. **应用配置文本**：NDEF Text 记录中的 `INFO1` 和 `CFG1` 文本，是本项目自定义的应用层格式，不是 NFC Forum 标准。

## 1. 手机端如何读写

使用 NFC 写卡软件时，选择 **NDEF Text / 文本记录**，输入本文中的文本内容。不要选择 URI、URL、MIME 或 Raw/原始字节模式，也不要把 NDEF 头部字节作为文本输入。

NDEF Text 记录的记录头由写卡软件自动编码。固件当前识别的记录采用单条、短格式、Well-Known Text 记录，头部布局为：

| 字节 | 含义 |
| --- | --- |
| `D1` | NDEF Record Header：单条记录的 MB、ME、SR 标志置位，TNF 为 Well-Known |
| `01` | Type 长度为 1 |
| `LL` | Payload 长度，随内容变化 |
| `54` (`T`) | Well-Known Text 记录类型 |
| `SS` | Text 状态字节；低 6 位是语言代码长度，高 2 位必须为 0 |
| `en` 等 | 语言代码，例如 `en` |
| ... | UTF-8 文本内容 |

因此，`D1 01 LL 54 ...` 是 NDEF 记录的标准编码结构示例；其中 `LL` 不是固定值。它不是应用配置语法。应用层文本从 Text 记录的文本负载中提取。

当前解析器要求单条短格式 Text 记录，且记录长度与 Payload 长度相符；多记录、长格式记录、其他记录类型不会被接受。

## 2. 标签读取内容

标签读取时返回一条 Text 记录，文本格式如下：

```text
INFO1;device_id=<芯片唯一ID>;node_id=1;mac=<芯片MAC>;uwb_start_node_id=1;uwb_end_node_id=10;min_interval_ms=100;max_interval_ms=20000;active_window_s=30;deep_sleep_s=1800;battery_level=0.0;battery_health=0.0
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `INFO1` | 读取信息的应用层格式标识 |
| `device_id` | 从芯片硬件信息接口读取的唯一 ID，只读 |
| `node_id` | 当前 `Dw1000_State.uiTagNodeID`，即当前 UWB 扫描的架号，只读 |
| `mac` | 芯片 MAC 地址，只读 |
| `uwb_start_node_id` | UWB 扫描起始架号，可写，范围 1–10 |
| `uwb_end_node_id` | UWB 扫描结束架号，可写，范围为起始架号–10 |
| `min_interval_ms` | UWB 最小发送间隔，可写，范围 1–20000 ms |
| `max_interval_ms` | UWB 最大发送间隔，可写，范围为最小间隔–20000 ms |
| `active_window_s` | 运动触发后 UWB 保持活动的时长，可写，范围 10–60 秒 |
| `deep_sleep_s` | 连续无运动后进入 ship mode 的等待时长，可写，范围 60–7200 秒 |
| `battery_level` | 电池电量百分比（SOC），只读 |
| `battery_health` | 电池健康度百分比（SOH），只读 |

数值为示例；设备实际返回当前运行值。电池数据在燃料计更新时刷新。

## 3. 写入配置格式

写入时，在 NFC 软件中创建**一条 Text 记录**，文本格式为：

```text
CFG1;pin=<PIN>;uwb_start_node_id=2;uwb_end_node_id=8;min_interval_ms=100;max_interval_ms=20000;active_window_s=30;deep_sleep_s=1800
```

例如，固件配置的 PIN 为 `12345678` 时：

```text
CFG1;pin=12345678;uwb_start_node_id=2;uwb_end_node_id=8;min_interval_ms=150;max_interval_ms=10000;active_window_s=45;deep_sleep_s=3600
```

`CFG1`、PIN 和配置字段都属于文本内容，字段之间用分号 `;` 分隔。每个配置字段均可省略；省略时保留设备当前值。PIN 必须提供，且必须与固件中的 PIN 完全一致。

### 支持字段与取值范围

| 字段 | 必填 | 允许范围 | 说明 |
| --- | --- | --- | --- |
| `pin` | 是 | 固件配置的 4–12 位数字 PIN | 写入校验；不作为保存的配置字段 |
| `uwb_start_node_id` | 否 | 1–10 | UWB 扫描起始架号 |
| `uwb_end_node_id` | 否 | 起始架号–10 | UWB 扫描结束架号 |
| `min_interval_ms` | 否 | 1–20000 | 自适应发送间隔下限 |
| `max_interval_ms` | 否 | 最小间隔–20000 | 自适应发送间隔上限 |
| `active_window_s` | 否 | 10–60 | 运动后 UWB 活动窗口时长，单位秒 |
| `deep_sleep_s` | 否 | 60–7200 | 无运动后进入 ship mode 的时间，单位秒 |

可只修改一个配置字段，例如：

```text
CFG1;pin=12345678;active_window_s=45;deep_sleep_s=3600
```

省略的字段保留当前配置。`node_id`、芯片 MAC、电量和健康度为只读信息，不能作为 CFG1 写入字段。起止架号与发送间隔必须满足起始架号不大于结束架号、最小间隔不大于最大间隔；活动窗口和深度休眠时长必须位于各自范围内。

纯 `CFG1;pin=...` 不含要修改的设置，验证 PIN 后不写入配置。

## 4. PIN 设置与安全说明

PIN 通过应用 Kconfig 字符串 `APP_NFC_CONFIG_PIN` 设置，对应配置符号为 `CONFIG_APP_NFC_CONFIG_PIN`。例如，在本地构建配置中设置：

```conf
CONFIG_APP_NFC_CONFIG_PIN="12345678"
```

需要重新构建并烧录固件后才生效。默认值为空；空值或不符合 4–12 位数字要求时，标签仍可读取，但配置写入会被拒绝。

PIN 编译进固件，属于简单写入门槛，不是强认证机制。不要将真实 PIN 提交到版本控制。

## 5. 校验与错误处理

以下情况会拒绝配置：

- NDEF 不是固件支持的单条短格式 Text 记录。
- 文本不以 `CFG1` 开始。
- 字段缺少 `=` 或字段值为空。
- 字段名不支持，或同一字段重复出现。
- PIN 未配置或与固件中的 PIN 不匹配。
- 数值不是十进制数字、超出范围，或起止架号/间隔上下限关系无效。
- 配置读取或持久化失败。

拒绝原因会通过串口日志输出。常见错误码包括 `-EINVAL`（格式或字段错误）、`-EACCES`（PIN 无效/未配置）以及 `-ERANGE`（数值超范围）。

成功写入后，固件将配置保存到现有存储分区，并刷新标签的 `INFO1` 信息。若设备已进入 PMIC ship mode，NFC 不可用；需先按设备现有唤醒方式启动。
