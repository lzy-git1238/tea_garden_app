# LoRa 通信协议说明

> 适用项目：智能家庭绿植管理系统  
> 源码位置：`test/loramanager.h` / `test/loramanager.cpp`

---

## 一、物理层

| 参数 | 值 | 环境变量 | 说明 |
|------|-----|----------|------|
| 接口 | UART 串口 | `TEA_LORA_PORT` | 默认 `/dev/ttyUSB0` |
| 波特率 | 9600 | `TEA_LORA_BAUD` | — |
| 数据位 | 8 | — | `QSerialPort::Data8` |
| 校验位 | 无 | — | `QSerialPort::NoParity` |
| 停止位 | 1 | — | `QSerialPort::OneStop` |
| 流控 | 无 | — | `QSerialPort::NoFlowControl` |

---

## 二、帧格式（数据链路层）

纯文本、换行分隔的帧格式：

- **帧分隔符**：`\n`（LF，0x0A）
- **字符编码**：UTF-8
- **缓冲区策略**：逐字节追加到 `rxBuffer`，按 `\n` 切分后逐行解析
- **粘包/截断处理**：不含 `\n` 的不完整行保留在缓冲区，等待后续数据

---

## 三、上行链路 —— 传感器数据上报

### 3.1 数据格式

传感器节点以自然语言风格文本行上报，每行一条或多条传感器读数。

### 3.2 字段定义

#### (1) 风速

```
正则: Wind\s+Speed\s*:\s*([-+]?\d+(?:\.\d+)?)\s*m\/s
格式: Wind Speed : <value> m/s
示例: Wind Speed : 3.5 m/s
单位: m/s
```

#### (2) 空气温湿度

```
正则: Temperature\s*:\s*([-+]?\d+(?:\.\d+)?)\s*C\s*,\s*Humidity\s*:\s*([-+]?\d+(?:\.\d+)?)\s*%
格式: Temperature : <value> C , Humidity : <value> %
示例: Temperature : 25.3 C , Humidity : 65.2 %
单位: 温度 ℃ / 湿度 %
捕获: 捕获组1 = 温度，捕获组2 = 湿度
```

#### (3) 光照强度

```
正则: (?:Initial|Total|Tal)?\s*Light\s+Intensity\s*:\s*([-+]?\d+(?:\.\d+)?)\s*lx
格式: [Total] Light Intensity : <value> lx
示例: Light Intensity : 3200 lx
      Total Light Intensity : 3200 lx
单位: lx（勒克斯）
```

#### (4) 土壤传感器数据

以 `Soil Sensor Data :`（兼容 `Oil Sensor Data :` OCR 误差）为行前缀，同一行内可包含以下任意组合：

| 子字段 | 正则 | 示例 | 单位 |
|--------|------|------|------|
| 土壤湿度 | `Humidity\s*:\s*([-+]?\d+(?:\.\d+)?)` | `Humidity : 42.5` | % |
| 土壤温度 | `Temperature\s*:\s*([-+]?\d+(?:\.\d+)?)` | `Temperature : 18.2` | ℃ |
| 土壤电导率 | `Conductivity\s*:\s*([-+]?\d+(?:\.\d+)?)` | `Conductivity : 1.35` | mS/cm |
| 土壤pH | `pH\s*:\s*([-+]?\d+(?:\.\d+)?)` | `pH : 5.6` | — |

完整示例：

```
Soil Sensor Data : Humidity : 42.5 Temperature : 18.2 Conductivity : 1.35 pH : 5.6
```

> 四个子字段顺序任意，正则按 湿度 → 温度 → 电导率 → pH 依次提取。

### 3.3 解析流程

```
串口数据到达
    │
    ▼
onReadyRead()
    │  readAll() → 追加到 rxBuffer
    ▼
parseTextLines()
    │  按 '\n' 切分
    ├── 不完整行 → 留在缓冲区
    └── 完整行 → 去空白，跳过空行
                   │
                   ▼
               parseTextLine()
                   │
                   ├── 匹配风速正则 → 更新 latestWindSpeed
                   ├── 匹配空气温湿度正则 → 更新 latestAirTemp, latestAirHumidity
                   ├── 匹配光照正则 → 更新 latestLightLux
                   ├── 匹配土壤前缀 → 依次匹配4个子字段
                   │
                   ├── 任一匹配成功 → emitLatestSnapshot()
                   │       └── emit sensorDataUpdated(全部缓存值)
                   │
                   └── 全部不匹配 → qDebug() + emit parseError(...)
```

### 3.4 信号输出

```cpp
void sensorDataUpdated(
    double windSpeed,         // 风速 (m/s)，无数据时为 NaN
    double airTemp,           // 空气温度 (℃)
    double airHumidity,       // 空气湿度 (%)
    double lightLux,          // 光照强度 (lx)
    double soilHumidity,      // 土壤湿度 (%)
    double soilTemp,          // 土壤温度 (℃)
    double soilConductivity,  // 土壤电导率 (mS/cm)
    double soilPh,            // 土壤pH
    const QDateTime &timestamp // 本地时间戳
);
```

### 3.5 增量更新机制

- `LoraManager` 内部维护 8 个 `latest*` 缓存变量，初始值均为 `NaN`
- 每次解析到任意字段后，**立即**将全部 8 个字段的当前缓存值全量发送
- 上层通过 `qIsFinite()` 判断每个字段是否有效，仅更新有效字段

---

## 四、下行链路 —— 设备控制命令

### 4.1 命令编码

| 整数值 | 含义 |
|--------|------|
| `1` | 开启灭蚊灯 |
| `2` | 关闭灭蚊灯 |
| `3` | 开启水泵 |
| `4` | 关闭水泵 |

### 4.2 帧格式

```
<整数值>\n
```

示例：

```
1\n    → 开启灭蚊灯
4\n    → 关闭水泵
```

### 4.3 高冗余发送

```cpp
const QByteArray payload = QByteArray::number(value) + "\n";
const int repeatCount = 100;           // 同一条命令重复发送 100 次
for (int i = 0; i < repeatCount; ++i) {
    serialPort->write(payload);
}
serialPort->flush();
```

> LoRa 无线链路存在丢包风险，重复 100 次以带宽换取可靠性。接收端应具备去重能力。

### 4.4 应用层防抖

同一设备连续命令之间设置最小发送间隔：

| 参数 | 默认值 | 环境变量 |
|------|--------|----------|
| 最小间隔 | 800 ms | `TEA_DEVICE_CMD_MIN_MS` |

- 在间隔内再次发送同一设备命令，排队延迟到间隔结束后执行
- 不同设备间隔独立计时（`pump` / `lamp` 互不影响）
- 排队期间重复命令以最后一次为准（覆盖写入）

---

## 五、容错与降级

| 机制 | 说明 |
|------|------|
| 文本协议容错 | 大小写不敏感；容忍 OCR 误差（`Soil`/`Oil`）；容忍前缀变体（`Total`/`Initial`/`Tal`） |
| 字段独立解析 | 每个传感器字段独立匹配，某字段缺失不影响其他字段 |
| 粘包处理 | 不完整行暂存缓冲区，等待后续数据 |
| 超时降级 | LoRa 数据超时 **10 秒** → 自动切换模拟数据，系统持续可用 |
| 错误静默 | 无法识别的行仅输出日志，不弹 UI 告警 |
| NaN 保护 | 缓存值初始为 `NaN`，通过 `qIsFinite()` 判断有效性后再刷新 UI |

---

## 六、数据流

```
 ┌──────────────────────────┐
 │   LoRa 无线传感器节点     │
 │  (风速/温湿度/光照/土壤)   │
 └────────────┬─────────────┘
              │ LoRa RF
 ┌────────────┴─────────────┐
 │   LoRa 接收模块           │
 │   → UART → /dev/ttyUSB0  │
 └────────────┬─────────────┘
              │ 文本行 (\n 分隔)
 ┌────────────┴─────────────┐
 │   LoraManager             │
 │   上行：正则文本解析       │
 │   下行：100次重复发送      │
 └────────────┬─────────────┘
              │ sensorDataUpdated
              │ statusChanged
              │ parseError
              ▼
 ┌──────────────────────────┐
 │   上层业务                 │
 │   (UI 刷新 / 设备控制)    │
 └──────────────────────────┘
```

---

## 七、协议特点

| 特性 | 上行（传感器） | 下行（控制） |
|------|---------------|-------------|
| 格式 | 自然语言风格富文本 | 极简整数 |
| 编码 | UTF-8 | ASCII 数字 + `\n` |
| 可靠性 | 正则匹配，容忍格式偏差 | 100次重复发送 |
| 可读性 | 人类可读，便于调试 | 紧凑高效 |
| 容错 | 模糊正则匹配 | 应用层防抖 |
| 扩展性 | 新增字段加正则 | 新增命令分配整数 |

---

## 八、源码索引

| 文件 | 内容 |
|------|------|
| `test/loramanager.h` | LoRa 管理器接口：信号声明、串口配置 |
| `test/loramanager.cpp` | LoRa 协议实现：正则定义、收发逻辑、帧解析 |
| `test/teagardensystem.h` | LoRa 数据消费方接口 |
| `test/teagardensystem.cpp` | 命令防抖、超时降级、UI 刷新 |
