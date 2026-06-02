# 智慧茶园 — 微信小程序开发文档

## 1. 数据来源

小程序所有数据从华为云 IoT 平台获取，无本地数据源。

| 数据 | 来源 | 说明 |
|------|------|------|
| 传感器实时值 | IoT 设备影子 / 属性 | tea_sensor 服务 |
| 病虫害统计 | IoT 设备影子 / 属性 | tea_pest 服务 |
| 24h 天气预报 | 星图云 API（直调） | 小程序端直接请求 |

---

## 2. IoT 平台连接参数

| 参数 | 值 |
|------|-----|
| 设备 ID | `6a1c28dec9429d337f4cf809_tea_elf2` |
| IoT 平台区域 | `cn-south-1`（广州） |
| MQTT Broker | `5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com:8883` |

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

**上报频率**：每 3 秒（本地 APP 实时上报）

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

shadow 响应体格式：
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
  }]
}
```

---

## 5. 页面设计

### 5.1 首页 — 数据仪表盘

```
┌─────────────────────────────┐
│    智慧茶园          ☀️ 32℃ │
├─────────────────────────────┤
│ ┌───────┐ ┌───────┐ ┌───────┐
│ │ 温度  │ │ 湿度  │ │ 光照  │
│ │ 26.5℃│ │ 60.2% │ │ 2000  │
│ └───────┘ └───────┘ └───────┘
│ ┌───────┐ ┌───────┐ ┌───────┐
│ │ 风速  │ │土温   │ │土湿   │
│ │ 2.5m/s│ │ 18.0℃│ │ 45.0% │
│ └───────┘ └───────┘ └───────┘
│          ┌───────┐
│          │ 电导率 │
│          │ 1.2   │
│          └───────┘
├────────────┬────────────────┤
│  🌤 预报   │  🐛 病虫害     │
│  多云 32℃ │  总计: 15 处   │
│            │  🟢茶尺蠖 6    │
│            │  🟠绿叶蝉 4    │
│            │  🔴炭疽病 5    │
├────────────┴────────────────┤
│  [传感器] [病虫害饼图] [地图]│  ← TabBar
└─────────────────────────────┘
```

### 5.2 传感器详情页

所有 7 项传感器以大卡片展示，每项显示名称+数值+单位。点击卡片可查看 24h 趋势（从 IoT 属性本身无法取历史，需额外调 IoT 的设备消息 API）。

### 5.3 病虫害页

```
┌──────────────────────────┐
│ 🐛 病虫害监测            │
│ 累计检测: 15 处          │
├──────────────────────────┤
│                     🟢茶尺蠖 6(40%)  │
│    🍩 饼图             🟠绿叶蝉 4(27%)│
│                       🔴炭疽病 5(33%)│
├──────────────────────────┤
│ 位置列表:
│ 🟢 茶尺蠖   06-02 17:04  严重 ⚠│
│ 🟠 绿叶蝉   06-02 17:01  轻微   │
│ 🔴 炭疽病   06-02 16:55  中等   │
└──────────────────────────┘
```

### 5.4 地图页

使用微信 `map` 组件，加载 `pest_locations` 数组作为标记点。

```xml
<map latitude="25.08" longitude="118.232"
     markers="{{pestMarkers}}" circles="{{circles}}"
     show-location style="width:100%;height:100vh;" />
```

标记点数据生成：
```javascript
this.setData({
  pestMarkers: locations.map((loc, i) => ({
    id: i,
    latitude:  parseFloat(loc.lat),
    longitude: parseFloat(loc.lng),
    title:    loc.type,
    iconPath: getIconColor(loc.type), // 本地图标路径
    callout:  { content: `${loc.type}\n${loc.time}\n严重程度:${loc.sev}`, fontSize: 12, color: '#333', bgColor: '#fff', borderRadius: 4, padding: 6, display: 'BYCLICK' }
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

## 6. 页面配色

| 元素 | 颜色 | 说明 |
|------|------|------|
| 背景 | `#F5F6F8` | 浅灰 |
| 卡片 | `#FFFFFF` | 白底圆角 12px |
| 标题 | `#2c3e50` | 深灰蓝 |
| 数值 | `#00BFA5` | 青绿 |
| 茶尺蠖 | `#4CAF50` | 绿 |
| 茶小绿叶蝉 | `#FF9800` | 橙 |
| 茶炭疽病 | `#F44336` | 红 |

---

## 7. 定时刷新

| 页面 | 刷新间隔 | 说明 |
|------|----------|------|
| 首页仪表盘 | 5 秒 | 传感器数值实时 |
| 病虫害饼图 | 30 秒 | 病虫害不频繁 |
| 地图 | 30 秒 | 与病虫害同步 |
| 天气预报 | 30 分钟 | 天气更新慢 |

用 `setInterval` + 页面 `onShow`/`onHide` 管理。

---

## 8. 你需要在华为云准备

| 事项 | 说明 |
|------|------|
| 1. IAM 账号 | 有 IoTDA 读取权限的子账号（推荐），或使用主账号 |
| 2. AccessKey/SecretKey | 控制台→我的凭证→访问密钥 |
| 3. Project ID | 控制台→我的凭证→项目 ID（cn-south-1） |
| 4. 微信小程序 AppID | 微信公众平台→开发→开发设置 |
| 5. 服务器域名白名单 | 小程序管理后台→开发→服务器域名→添加 `5fa66df6c4.st1.iotda-device.cn-south-1.myhuaweicloud.com` 和 `api.open.geovisearth.com` |
