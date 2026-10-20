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
INFO1;model=SignalConvert-R4;device_id=<芯片唯一ID>;device_number=0;local_id=0;connect_type=0;tx_power=0;max_connections=10
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `INFO1` | 读取信息的应用层格式标识 |
| `model` | 产品型号 |
| `device_id` | 从芯片硬件信息接口读取的唯一 ID；不能通过 NFC 修改 |
| `device_number` | 可配置的设备编号，0–255 |
| `local_id` | 本地设备 ID，0–255 |
| `connect_type` | 连接类型参数，0–15 |
| `tx_power` | 发射功率参数，0–8 |
| `max_connections` | 最大连接数参数，0–15 |

示例中的配置值仅用于展示；设备实际返回其当前配置。

## 3. 写入配置格式

写入时，在 NFC 软件中创建**一条 Text 记录**，文本格式为：

```text
CFG1;pin=<PIN>;local_id=12;device_number=34;connect_type=2;tx_power=4;max_connections=5
```

例如，固件配置的 PIN 为 `12345678` 时：

```text
CFG1;pin=12345678;local_id=12;device_number=34;connect_type=2;tx_power=4;max_connections=5
```

`CFG1`、PIN 和配置字段都属于文本内容，字段之间用分号 `;` 分隔。每个配置字段均可省略；省略时保留设备当前值。PIN 必须提供，且必须与固件中的 PIN 完全一致。

### 支持字段与取值范围

| 字段 | 必填 | 允许范围 | 说明 |
| --- | --- | --- | --- |
| `pin` | 是 | 固件配置的 4–12 位数字 PIN | 写入校验；不作为保存的配置字段 |
| `local_id` | 否 | 0–255 | 本地设备 ID |
| `device_number` | 否 | 0–255 | 设备编号 |
| `connect_type` | 否 | 0–15 | 连接类型参数 |
| `tx_power` | 否 | 0–8 | 发射功率参数 |
| `max_connections` | 否 | 0–15 | 最大连接数参数 |

可只修改一个配置字段，例如：

```text
CFG1;pin=12345678;local_id=12
```

纯 `CFG1;pin=...` 不含任何要修改的配置项，当前实现会接受该记录但不写入配置。

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
- 数值不是十进制数字，或超出该字段允许范围。
- 配置读取或持久化失败。

拒绝原因会通过串口日志输出。常见错误码包括 `-EINVAL`（格式或字段错误）、`-EACCES`（PIN 无效/未配置）以及 `-ERANGE`（数值超范围）。

成功写入后，固件将配置保存到现有存储分区，并刷新标签的 `INFO1` 信息。若设备已进入 PMIC ship mode，NFC 不可用；需先按设备现有唤醒方式启动。
