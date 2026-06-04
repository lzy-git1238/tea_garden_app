# 智慧茶园 — 微信小程序开发文档

> 更新时间：2026-06-04（匹配桌面端 v2026-06-04）

## 1. 数据来源

小程序所有数据从华为云 IoT 平台获取，无本地数据源。

| 数据 | 来源 | 说明 |
|------|------|------|
| 传感器实时值 | IoT 设备影子 / 属性 | tea_sensor 服务 |
| 病虫害统计 | IoT 设备影子 / 属性 | tea_pest 服务 |
| 设备开关状态 | IoT 设备影子 / 属性 | tea_device 服务 |
| 24h 天气预报 | 星图云 API（直调） | 小程序端直接请求 |

---

## 2. IoT 平台连接参数

| 参数 | 值 |
|------|-----|
| 设备 ID | `6a1c28dec9429d337f4cf809_tea_elf2` |
| IoT 平台区域 | `cn-south-1`（广州） |
| MQTT Broker | `5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com:8883` |

```
{
    "device_id": "6a1c28dec9429d337f4cf809_tea_elf2",
    "secret": "92380c03765136070405fcb3cff92e45"
}
{
    "username": "6a1c28dec9429d337f4cf809_tea_elf2",
    "password": "35930a355b07d9e957f58e6af8ae12b9ea7d5f26e64ed1d374fc8562f42509db",
    "clientId": "6a1c28dec9429d337f4cf809_tea_elf2_0_0_2026053112",
    "hostname": "5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com",
    "port": 8883,
    "protocol": "MQTTS"
}
```

> 微信小程序不支持 MQTT 直连。推荐通过**华为云 API 获取设备影子数据**（HTTPS），详见第 4 节。

---

## 3. 数据结构

### 3.1 tea_sensor — 传感器服务

| 属性名 | 类型 | 单位 | 说明 |
|--------|------|------|------|
| `air_temp` | float | ℃ | 空气温度 |
| `air_hum` | float | % | 空气湿度 |
| `light_intensity` | float | Lux | 光照强度 |
| `wind_speed` | float | m/s | 风速 |
| `soli_temp` | float | ℃ | 土壤温度 |
| `soli_hum` | float | % | 土壤湿度 |
| `soli_ec` | float | mS/cm | 土壤电导率 |

**上报频率**：每 3 秒（桌面端实时上报）

### 3.2 tea_pest — 病虫害服务

| 属性名 | 类型 | 说明 |
|--------|------|------|
| `pest_counts` | string (JSON 对象) | 每种病虫害→发生次数 |
| `pest_locations` | string (JSON 数组) | 最近 20 条检测记录 |
| `pest_total` | int | 累计总次数 |

**pest_counts 示例**：
```json
{"茶尺蠖":6,"茶小绿叶蝉":4,"茶炭疽病":5}
```

**pest_locations 示例**：
```json
[
  {"lat":"25.088300","lng":"118.233500","type":"茶尺蠖","sev":2,"time":"06-02 17:04"},
  {"lat":"25.079200","lng":"118.241200","type":"茶小绿叶蝉","sev":1,"time":"06-02 17:01"}
]
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `lat` | string | 纬度（6 位小数） |
| `lng` | string | 经度（6 位小数） |
| `type` | string | 病虫害种类 |
| `sev` | int | 严重程度 1=轻微 2=中等 3=严重 |
| `time` | string | 检测时间 `MM-dd HH:mm` |

### 3.3 tea_device — 设备状态服务（新增）

| 属性名 | 类型 | 说明 |
|--------|------|------|
| `pump_state` | bool | 灌溉泵开关，true=开 false=关 |
| `lamp_state` | bool | 驱虫灯开关，true=开 false=关 |

**上报时机**：每次开关状态变化时即时上报（来源包括：云端命令、手动按钮、定时计划），确保云端始终保持真实状态。

---

## 4. 获取 IoT 数据（HTTPS 方式）

微信小程序通过华为云 IoT **设备影子查询 API** 获取属性值。

### 4.1 API 认证（Token 获取）

```
POST https://iam.cn-south-1.myhuaweicloud.com/v3/auth/tokens
Content-Type: application/json

{
  "auth": {
    "identity": {
      "methods": ["password"],
      "password": {
        "user": {
          "name": "你的华为云用户名",
          "password": "你的华为云密码",
          "domain": { "name": "你的华为云账号名" }
        }
      }
    },
    "scope": {
      "project": { "name": "cn-south-1" }
    }
  }
}
```

返回头 `X-Subject-Token` 即为 IAM Token（有效期 24 小时）。

### 4.2 查询设备影子

```
GET https://5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com/v5/iot/{project_id}/devices/{device_id}/shadow
Authorization: Bearer {iam_token}
```

或通过 SDK 简化调用（推荐）：

```javascript
// 安装: npm install @huaweicloud/huaweicloud-sdk-iotda
const IoTDA = require('@huaweicloud/huaweicloud-sdk-iotda');

const client = IoTDA.IoTDAClient.newBuilder()
  .withCredential(new IoTDA.BasicCredentials()
    .withAk("你的AccessKey")
    .withSk("你的SecretKey")
    .withProjectId("你的项目ID"))
  .withEndpoint("5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com")
  .build();

// 查询设备影子
const req = new IoTDA.ShowDeviceShadowRequest();
req.setDeviceId("6a1c28dec9429d337f4cf809_tea_elf2");
const resp = await client.showDeviceShadow(req);
const shadow = JSON.parse(resp.shadow);  // 全量属性
```

shadow 响应体格式（更新后包含 3 个服务）：
```json
{
  "shadow": [{
    "service_id": "tea_sensor",
    "desired": {},
    "reported": {
      "air_temp": 26.5,
      "air_hum": 60.2,
      "light_intensity": 2000,
      "wind_speed": 2.5,
      "soli_temp": 18.0,
      "soli_hum": 45.0,
      "soli_ec": 1.2
    }
  }, {
    "service_id": "tea_pest",
    "desired": {},
    "reported": {
      "pest_counts": "{\"茶尺蠖\":6}",
      "pest_locations": "[{\"lat\":\"25.08\",...}]",
      "pest_total": 6
    }
  }, {
    "service_id": "tea_device",
    "desired": {},
    "reported": {
      "pump_state": false,
      "lamp_state": true
    }
  }]
}
```

---

## 5. 设备控制 — 命令下发（新增）

小程序可以通过华为云 IoT **设备命令下发 API** 远程控制灌溉泵和灭虫灯。

### 5.1 命令格式

| 命令 | 服务 ID | 命令名 | 参数 | 类型 | 说明 |
|------|---------|--------|------|------|------|
| 水泵开/关 | `pump_control` | `pump_control` | `pump` | bool | true=开 false=关 |
| 驱虫灯开/关 | `light_control` | `light_control` | `light` | bool | true=开 false=关 |

### 5.2 下发命令 API

**HTTPS SDK 方式（推荐）**：

```javascript
// 创建命令请求
const req = new IoTDA.CreateCommandRequest();
req.setDeviceId("6a1c28dec9429d337f4cf809_tea_elf2");

const body = new IoTDA.DeviceCommandRequest();
body.setServiceId("pump_control");
body.setCommandName("pump_control");
// 参数：paras → 注意华为云 IoT 命令 paras 字段名与参数名一致
body.setParas(JSON.stringify({ pump: true }));

req.setBody(body);
const resp = await client.createCommand(req);
// 响应中 code==200 表示命令已下发
```

**HTTPS REST API 方式**：

```
POST https://5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com/v5/iot/{project_id}/devices/{device_id}/commands
Authorization: Bearer {iam_token}
Content-Type: application/json

{
  "service_id": "pump_control",
  "command_name": "pump_control",
  "paras": { "pump": true }
}
```

### 5.3 命令响应

设备端收到命令后通过 MQTT（桌面 APP 端 Paho MQTT C++）发布响应到：

```
$oc/devices/{device_id}/sys/commands/response/request_id={request_id}
```

响应格式：

```json
{
  "result_code": 0,
  "response_name": "pump_control",
  "paras": { "result": 0 }
}
```

| 字段 | 说明 |
|------|------|
| `result_code` | 0 = 平台通信成功 |
| `result` | 0 = 硬件层执行成功，1 = 执行失败（如 LoRa 离线） |

**小程序侧**：命令下发后可通过查询 API 获取命令状态，或直接查 `tea_device` 影子确认设备状态已变更。

---

## 6. 设计风格 — 现代农业科技风

小程序整体采用**深色科技风**，参考智慧农业驾驶舱的视觉语言，与桌面 APP 风格统一。

**核心元素**：
- 深色基底（`#0a1628` 深海军蓝），营造专业数据看板氛围
- 科技青/荧光绿强调色（`#00d4ff`、`#4caf50`），突出数据和交互元素
- 半透明毛玻璃卡片（`rgba(10,35,70,0.55)`），层次分明
- 圆角 8–12px，柔和但不失科技感
- 数据大字展示，配合细线分隔和渐变进度条

---

## 7. 页面配色

| 用途 | 色值 | 说明 |
|------|------|------|
| 页面背景 | `#0a1628` | 深海军蓝，暗色科技风 |
| 卡片背景 | `rgba(10, 35, 70, 0.55)` | 半透明毛玻璃 |
| 卡片边框 | `#1a4a78` | 蓝灰边框 |
| 强调色（主） | `#00d4ff` | 科技青，标题 / 选中态 |
| 强调色（次） | `#4caf50` | 荧光绿，数据高亮 / 开关开启 |
| 文字主色 | `#E8F4FF` | 浅蓝白 |
| 文字辅色 | `#7eb8d4` | 淡蓝灰，标签 / 单位 |
| 文字弱色 | `#507080` | 深灰蓝，次要信息 |
| 危险/关闭 | `#ef5350` | 红灯提示 / 开关关闭 |
| 进度条 | `linear-gradient(#4caf50, #81c784)` | 绿色渐变 |
| 茶尺蠖标记 | `#4CAF50` | 绿 |
| 茶小绿叶蝉标记 | `#FF9800` | 橙 |
| 茶炭疽病标记 | `#F44336` | 红 |
| 水泵开启 | `#00d4ff` bg + `#0a1628` text | 科技青按钮 |
| 水泵关闭 | `rgba(10,35,70,0.5)` | 半透明暗色 |
| 驱虫灯开启 | `#ff9800` bg + `#0a1628` text | 暖橙按钮 |
| 驱虫灯关闭 | `rgba(10,35,70,0.5)` | 半透明暗色 |
| TabBar 背景 | `#0d1f2d` | 深蓝黑，比页面略亮 |
| TabBar 选中 | `#00d4ff` | 科技青 |

---

## 8. 页面设计

### 8.1 全局样式参考

```css
/* app.wxss — 全局样式 */
page {
  background: #0a1628;
  color: #E8F4FF;
  font-family: -apple-system, 'PingFang SC', 'Microsoft YaHei', sans-serif;
}

.card {
  background: rgba(10, 35, 70, 0.55);
  border: 1px solid #1a4a78;
  border-radius: 10px;
  padding: 16rpx 20rpx;
  margin: 12rpx;
  box-shadow: 0 2rpx 16rpx rgba(0, 212, 255, 0.06);
}

.section-title {
  font-size: 30rpx;
  font-weight: bold;
  color: #00d4ff;
  padding: 16rpx 24rpx 8rpx;
}

.data-value {
  font-size: 52rpx;
  font-weight: bold;
  color: #E8F4FF;
}

.data-unit {
  font-size: 22rpx;
  color: #7eb8d4;
  margin-left: 6rpx;
}

.data-label {
  font-size: 24rpx;
  color: #7eb8d4;
}

/* 设备按钮通用 */
.btn-on {
  background: #00d4ff;
  color: #0a1628;
  border-radius: 8rpx;
}

.btn-off {
  background: rgba(10, 35, 70, 0.5);
  color: #7eb8d4;
  border: 1px solid #1a4a78;
  border-radius: 8rpx;
}
```

### 8.2 首页 — 数据仪表盘

```
┌──────────────────────────────────┐
│  ▎ 智慧茶园综合管理系统          │  ← header，底部 #1a6fa8 细线
│     珠海·斗门          ☀️ 32℃    │  ← 天气摘要
├──────────────────────────────────┤
│  ▎ 环境监测                      │  ← section-title, 科技青
│  ┌──────┐┌──────┐┌──────┐┌──────┐│
│  │ 温度 ││ 湿度 ││ 光照 ││ 风速 ││  ← 毛玻璃卡片
│  │26.5℃││60.2%││ 2000││2.5   ││    数值大字白色
│  └──────┘└──────┘└──────┘└──────┘│
│  ┌──────┐┌──────┐┌──────┐       │
│  │土温  ││土湿  ││电导率│       │
│  │18.0℃││45.0%││ 1.2  │       │
│  └──────┘└──────┘└──────┘       │
├──────────────────────────────────┤
│  ▎ 设备控制                      │
│  ┌────────────────────────────┐  │
│  │ 💧 灌溉泵        ● 已关闭  │  │  ← 状态指示灯（绿/灰圆点）
│  │         [ 开启 ] [ 关闭 ]  │  │  ← 科技青按钮 / 暗色按钮
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │ 🪲 驱虫灯        ● 已开启  │  │
│  │         [ 开启 ] [ 关闭 ]  │  │
│  └────────────────────────────┘  │
├──────────────┬───────────────────┤
│  🌤 24h预报  │  🐛 病虫害       │
│  多云 32℃   │  累计: 15 处     │
│  珠海·斗门  │  ━━━━━━━━━━━━    │
│              │  🟢茶尺蠖 6  40% │  ← 带进度条
│              │  🟠绿叶蝉 4  27% │
│              │  🔴炭疽病 5  33% │
├──────────────┴───────────────────┤
│  📊 数据  │  🐛 虫情  │  🗺 地图 │  ← TabBar, 深色底+科技青选中
└──────────────────────────────────┘
```

**设备控制逻辑**：
- 状态指示灯：绿色 `#4caf50` 呼吸圆点=运行中，灰色 `#507080`=关闭
- 按钮态：当前状态对应按钮高亮，另一个暗色
- 读取 `tea_device` 影子显示当前状态
- 点击按钮 → 调命令下发 API
- 每次状态变化，桌面端自动上报 `tea_device`，小程序轮询同步

### 8.3 传感器详情页

7 项传感器各自为一张毛玻璃大卡片，居中大字显示当前值，下方淡色单位。点击卡片展开 24h 趋势图（需调 IoT 设备消息 API 取历史）。

```
┌────────────────────────────┐
│  ▎ 传感器详情              │
│                            │
│  ┌──────────────────────┐  │
│  │                      │  │
│  │      空气温度         │  │  ← 毛玻璃卡片
│  │       26.5           │  │  ← 52rpx 白色大字
│  │        ℃             │  │  ← 单位淡蓝
│  │  ▔▔▔▔▔▔▔▔▔▔▔▔▔▔   │  │  ← 点击展开趋势
│  └──────────────────────┘  │
│  ┌──────────────────────┐  │
│  │      空气湿度         │  │
│  │       60.2           │  │
│  │        %             │  │
│  └──────────────────────┘  │
│  ... (7 个卡片滚动)        │
└────────────────────────────┘
```

### 8.4 病虫害页

```
┌──────────────────────────────┐
│  ▎ 病虫害监测                │
│     累计检测: 15 处          │
├──────────────────────────────┤
│        ┌────────┐            │
│        │  🍩    │            │  ← ECharts 饼图，暗色背景
│        │ 饼图   │            │
│        └────────┘            │
│  🟢 茶尺蠖   6    40% ████  │  ← 色条进度
│  🟠 茶小绿叶蝉 4  27% ██    │
│  🔴 茶炭疽病   5    33% ███  │
├──────────────────────────────┤
│  检测记录                    │
│  ┌────────────────────────┐  │
│  │ 🟢 茶尺蠖   06-02 17:04│  │  ← 毛玻璃列表卡
│  │    严重 ⚠              │  │
│  └────────────────────────┘  │
│  ┌────────────────────────┐  │
│  │ 🟠 绿叶蝉   06-02 17:01│  │
│  │    轻微                │  │
│  └────────────────────────┘  │
│  ┌────────────────────────┐  │
│  │ 🔴 炭疽病   06-02 16:55│  │
│  │    中等                │  │
│  └────────────────────────┘  │
└──────────────────────────────┘
```

### 8.5 地图页

使用微信 `map` 组件 + 暗色风格图层，加载 `pest_locations` 数组作为标记点。茶园中心绘制半透明青色圆圈。

```xml
<map latitude="25.08" longitude="118.232"
     markers="{{pestMarkers}}" circles="{{circles}}"
     show-location
     style="width:100%;height:100vh;" />
```

```javascript
// 标记点数据（科技风暗色 callout）
this.setData({
  pestMarkers: locations.map((loc, i) => ({
    id: i,
    latitude:  parseFloat(loc.lat),
    longitude: parseFloat(loc.lng),
    title:    loc.type,
    iconPath: getIconColor(loc.type),
    width: 28, height: 28,
    callout: {
      content: `${loc.type}\n${loc.time}\n严重: ${['','轻微','中等','严重'][loc.sev]}`,
      fontSize: 12, color: '#E8F4FF', bgColor: '#0d1f2d',
      borderRadius: 6, padding: 8, display: 'BYCLICK'
    }
  })),
  circles: [{
    latitude:  25.08039,
    longitude: 118.232478,
    radius:    300,
    color:     '#00d4ff33',
    fillColor: '#00d4ff11'
  }]
});
```

---

## 8. 定时刷新

| 页面 | 刷新间隔 | 说明 |
|------|----------|------|
| 首页仪表盘 | 5 秒 | 传感器数值实时 |
| 设备控制区 | 5 秒 | 与仪表盘同步 |
| 病虫害饼图 | 30 秒 | 病虫害不频繁 |
| 地图 | 30 秒 | 与病虫害同步 |
| 天气预报 | 30 分钟 | 天气更新慢 |

用 `setInterval` + 页面 `onShow`/`onHide` 管理。

---

## 9. 你需要在华为云准备

| 事项 | 说明 |
|------|------|
| 1. IAM 账号 | 有 IoTDA 读取权限的子账号（推荐），或使用主账号 |
| 2. AccessKey/SecretKey | 控制台→我的凭证→访问密钥 |
| 3. Project ID | 控制台→我的凭证→项目 ID（cn-south-1） |
| 4. 微信小程序 AppID | 微信公众平台→开发→开发设置 |
| 5. 服务器域名白名单 | 添加 `5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com` 和 `api.open.geovisearth.com` |
| 6. IoT 产品模型 | 在华为云 IoTDA 控制台创建以下服务： |
|  | `tea_sensor`（7 个 float 属性） |
|  | `tea_pest`（pest_counts/pest_locations/pest_total） |
|  | `tea_device`（pump_state/lamp_state，见 3.3 节） |
|  | `pump_control` 命令（pump: bool） |
|  | `light_control` 命令（light: bool） |
