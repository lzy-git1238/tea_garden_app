# 网页远程监控 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** RK3588 内嵌 HTTP+WebSocket 服务，局域网浏览器远程查看茶园数据、视频、病虫害检测，控制设备和小车。

**Architecture:** Qt 端新增 WebServer 类（HTTP 静态文件 + WebSocket 实时推送），前端纯 HTML/JS，视频 MJPEG 推流 + YOLO bbox JSON 分离传输。

**Tech Stack:** Qt C++ (QTcpServer, QWebSocketServer), 前端 HTML/CSS/JS (Chart.js)

---

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `test/webserver.h` | 新增 | WebServer 类声明 |
| `test/webserver.cpp` | 新增 | HTTP + WebSocket 实现 |
| `test/web/index.html` | 新增 | 前端监控页面 |
| `test/teagardensystem.h` | 修改 | 添加 WebServer* 成员 |
| `test/teagardensystem.cpp` | 修改 | 初始化 + 数据推送 |
| `test/test.pro` | 修改 | 添加 webserver 源文件 + web resources |
| `test/resources.qrc` | 修改 | 添加 web/index.html |

---

### Task 1: WebServer 类

**Files:**
- Create: `test/webserver.h`
- Create: `test/webserver.cpp`

✅ 已完成。提供 HTTP 静态文件服务 + WebSocket 双向通信 + 视频帧 MJPEG 推送。

- [ ] **Step 1: Commit**

```bash
git add test/webserver.h test/webserver.cpp
git commit -m "feat: WebServer 类（HTTP + WebSocket）"
```

---

### Task 2: 前端监控页面

**Files:**
- Create: `test/web/index.html`

- [ ] **Step 1: 创建 index.html**

```html
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>智慧茶园远程监控</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { background:#0a1628; color:#E8F4FF; font-family:'Microsoft YaHei',sans-serif; }
.header { text-align:center; padding:8px; background:#0d2137; border-bottom:1px solid #1a4060; }
.header h1 { font-size:18px; color:#00d4ff; }
.row { display:flex; gap:8px; padding:8px; flex-wrap:wrap; }
.col { flex:1; min-width:300px; background:#0d1f30; border:1px solid #1a3040; border-radius:8px; padding:8px; }
.col-title { color:#00d4ff; font-weight:bold; margin-bottom:6px; font-size:14px; }
.video-box { background:#000; border:1px solid #1a3040; border-radius:4px; position:relative; overflow:hidden; }
.video-box img { width:100%; display:block; }
.video-box canvas { position:absolute; top:0; left:0; width:100%; height:100%; pointer-events:none; }
.cards { display:flex; gap:6px; flex-wrap:wrap; }
.card { flex:1; min-width:90px; background:#0a1420; border:1px solid #1a3040; border-radius:6px; padding:6px; text-align:center; }
.card .val { font-size:18px; font-weight:bold; color:#00d4ff; }
.card .unit { font-size:10px; color:#888; }
.pest-chart { height:180px; margin-top:8px; }
.btn-row { display:flex; gap:6px; margin-top:8px; flex-wrap:wrap; }
.btn { padding:8px 14px; border-radius:6px; border:none; color:#fff; font-size:13px; cursor:pointer; font-weight:bold; }
.btn-pump { background:#1565C0; }
.btn-lamp { background:#E65100; }
.btn-rover { background:#2E7D32; }
.btn-spray { background:#1E88E5; }
.btn:active { opacity:0.8; }
.status { font-size:11px; color:#666; text-align:center; padding:4px; }
</style>
</head>
<body>
<div class="header"><h1>🌿 智慧茶园远程监控</h1></div>
<div class="row">
  <!-- 无人机视频 -->
  <div class="col">
    <div class="col-title">🛸 农业无人机实时监控</div>
    <div class="video-box" id="droneBox">
      <img id="droneImg" src="" alt="等待视频流...">
      <canvas id="droneCanvas"></canvas>
    </div>
  </div>
  <!-- 小车图传 -->
  <div class="col">
    <div class="col-title">🚗 农业检测车实时监控</div>
    <div class="video-box" id="roverBox">
      <img id="roverImg" src="" alt="等待图传...">
    </div>
    <!-- 病虫害饼图 -->
    <div class="col-title" style="margin-top:8px;">🐛 病虫害统计</div>
    <div class="pest-chart"><canvas id="pestChart"></canvas></div>
  </div>
  <!-- 环境数据 + 控制 -->
  <div class="col">
    <div class="col-title">📊 环境数据</div>
    <div class="cards" id="sensorCards">
      <div class="card"><div class="val" id="temp">--</div><div class="unit">温度 ℃</div></div>
      <div class="card"><div class="val" id="humi">--</div><div class="unit">湿度 %</div></div>
      <div class="card"><div class="val" id="light">--</div><div class="unit">光照 lux</div></div>
      <div class="card"><div class="val" id="soilHumi">--</div><div class="unit">土壤湿度 %</div></div>
      <div class="card"><div class="val" id="soilTemp">--</div><div class="unit">土壤温度 ℃</div></div>
      <div class="card"><div class="val" id="ph">--</div><div class="unit">土壤 pH</div></div>
    </div>
    <!-- 天气预报 -->
    <div class="col-title" style="margin-top:8px;">🌤 天气预报</div>
    <div id="weatherInfo" style="font-size:12px; color:#90A4AE;">加载中...</div>
    <!-- 设备控制 -->
    <div class="col-title" style="margin-top:8px;">⚙ 设备控制</div>
    <div class="btn-row">
      <button class="btn btn-pump" id="btnPump">💧 灌溉泵</button>
      <button class="btn btn-lamp" id="btnLamp">🪲 驱虫灯</button>
    </div>
    <!-- 小车控制 -->
    <div class="col-title" style="margin-top:8px;">🚗 小车控制 (WASD)</div>
    <div class="btn-row" style="display:grid; grid-template-columns:repeat(3,1fr); gap:4px;">
      <div></div><button class="btn btn-rover" id="btnFwd">W 前进</button><div></div>
      <button class="btn btn-rover" id="btnLeft">A 左转</button>
      <button class="btn btn-rover" id="btnBack">S 后退</button>
      <button class="btn btn-rover" id="btnRight">D 右转</button>
    </div>
    <div class="btn-row" style="margin-top:4px;">
      <button class="btn btn-spray" id="btnSpray">💧 喷水</button>
    </div>
  </div>
</div>
<div class="status" id="status">连接中...</div>

<script>
// ── WebSocket ──
var ws, pestCounts = {};
var yoloBoxes = [];

function connect() {
  var proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(proto + '//' + location.host);
  ws.binaryType = 'arraybuffer';

  ws.onopen = function() { document.getElementById('status').textContent = '🟢 已连接'; };
  ws.onclose = function() { document.getElementById('status').textContent = '🔴 断开，3s后重连...'; setTimeout(connect, 3000); };

  ws.onmessage = function(e) {
    if (e.data instanceof ArrayBuffer) {
      // 二进制帧
      var buf = new Uint8Array(e.data);
      var str = String.fromCharCode.apply(null, buf.slice(0, 10));
      if (str.startsWith('FRAME:')) {
        var idx = str.indexOf(':', 6);
        var len = parseInt(str.substring(6, idx));
        var jpeg = buf.slice(6 + (idx-6) + 1, 6 + (idx-6) + 1 + len);
        var blob = new Blob([jpeg], {type:'image/jpeg'});
        document.getElementById('droneImg').src = URL.createObjectURL(blob);
        drawYoloBoxes();
      } else if (str.startsWith('ROVER:')) {
        var idx2 = str.indexOf(':', 6);
        var len2 = parseInt(str.substring(6, idx2));
        var jpeg2 = buf.slice(6 + (idx2-6) + 1, 6 + (idx2-6) + 1 + len2);
        var blob2 = new Blob([jpeg2], {type:'image/jpeg'});
        document.getElementById('roverImg').src = URL.createObjectURL(blob2);
      }
    } else {
      // JSON 消息
      var msg = JSON.parse(e.data);
      switch (msg.type) {
        case 'sensor': updateSensors(msg.data); break;
        case 'weather': updateWeather(msg.data); break;
        case 'yolo':
          yoloBoxes = msg.detections;
          updatePestChart(msg.detections);
          break;
      }
    }
  };
}

// ── 传感器更新 ──
function updateSensors(d) {
  document.getElementById('temp').textContent = d.airTemp.toFixed(1);
  document.getElementById('humi').textContent = d.airHumidity.toFixed(0);
  document.getElementById('light').textContent = d.lightLux.toFixed(0);
  document.getElementById('soilHumi').textContent = d.soilHumidity.toFixed(0);
  document.getElementById('soilTemp').textContent = d.soilTemp.toFixed(1);
  document.getElementById('ph').textContent = d.soilPh.toFixed(1);
}

// ── 天气更新 ──
function updateWeather(d) {
  document.getElementById('weatherInfo').innerHTML =
    d.icon + ' ' + d.desc + ' ' + d.temp + '℃ | 湿度 ' + d.rh + '% | ' + d.wind;
}

// ── 病虫害饼图 ──
var pestChartObj = null;
function updatePestChart(detections) {
  var ctx = document.getElementById('pestChart').getContext('2d');
  if (!pestChartObj) {
    pestChartObj = new Chart(ctx, {type:'pie', data:{labels:[],datasets:[{data:[],backgroundColor:['#F44336','#FF9800','#2196F3','#4CAF50','#9E9E9E','#E91E63']}]},
      options:{plugins:{legend:{labels:{color:'#90A4AE'}}},responsive:true,maintainAspectRatio:false}});
  }
  // 累计统计
  detections.forEach(function(d) {
    var cls = d.class;
    if (!pestCounts[cls]) pestCounts[cls] = 0;
    pestCounts[cls]++;
  });
  var labels = Object.keys(pestCounts).filter(function(k){return k!=='Healthy';});
  var data = labels.map(function(k){return pestCounts[k];});
  pestChartObj.data.labels = labels;
  pestChartObj.data.datasets[0].data = data;
  pestChartObj.update();
}

// ── YOLO 框叠加 ──
function drawYoloBoxes() {
  var img = document.getElementById('droneImg');
  var canvas = document.getElementById('droneCanvas');
  var ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (!img.naturalWidth || yoloBoxes.length===0) return;
  canvas.width = img.clientWidth;
  canvas.height = img.clientHeight;
  var sx = img.clientWidth / img.naturalWidth;
  var sy = img.clientHeight / img.naturalHeight;
  var colors = {'Algal Leaf Spot':'#2196F3','Brown Blight':'#F44336','Gray Blight':'#9E9E9E','Helopeltis':'#FF9800','Red Leaf Spot':'#E91E63','Healthy':'#4CAF50'};
  yoloBoxes.forEach(function(d) {
    var x = d.x1*sx, y = d.y1*sy, w = (d.x2-d.x1)*sx, h = (d.y2-d.y1)*sy;
    var c = colors[d.class] || '#FFF';
    ctx.strokeStyle = c; ctx.lineWidth = 2;
    ctx.strokeRect(x, y, w, h);
    ctx.fillStyle = c; ctx.font = '12px sans-serif';
    ctx.fillText(d.class + ' ' + (d.score*100).toFixed(0) + '%', x, y-4);
  });
}

// ── 设备控制 ──
function sendCmd(cmd, data) { if (ws && ws.readyState===1) ws.send(JSON.stringify({cmd:cmd,data:data})); }
function roverSend(key, down) { sendCmd('rover', {key:key, down:down}); }

document.getElementById('btnPump').onclick = function(){ sendCmd('pump','toggle'); };
document.getElementById('btnLamp').onclick = function(){ sendCmd('lamp','toggle'); };
document.getElementById('btnSpray').onmousedown = function(){ sendCmd('spray','on'); };
document.getElementById('btnSpray').onmouseup = function(){ sendCmd('spray','off'); };

['btnFwd','btnBack','btnLeft','btnRight'].forEach(function(id, i) {
  var keys = ['w','s','a','d'];
  document.getElementById(id).onmousedown = function(){ roverSend(keys[i], true); };
  document.getElementById(id).onmouseup   = function(){ roverSend(keys[i], false); };
});

// 键盘 WASD
document.onkeydown = function(e) {
  var map = {87:'w',65:'a',83:'s',68:'d'};
  if (map[e.keyCode]) { e.preventDefault(); roverSend(map[e.keyCode], true); }
};
document.onkeyup = function(e) {
  var map = {87:'w',65:'a',83:'s',68:'d'};
  if (map[e.keyCode]) { e.preventDefault(); roverSend(map[e.keyCode], false); }
};

connect();
setInterval(function() { drawYoloBoxes(); }, 100);
</script>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add test/web/index.html
git commit -m "feat: 前端监控页面"
```

---

### Task 3: resources.qrc 添加 web 资源

**Files:**
- Modify: `test/resources.qrc`

- [ ] **Step 1: 添加 web/index.html**

```xml
<file>web/index.html</file>
```

- [ ] **Step 2: Commit**

```bash
git add test/resources.qrc
git commit -m "build: 添加 web 资源到 qrc"
```

---

### Task 4: test.pro 添加 webserver

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: SOURCES + HEADERS**

```
SOURCES += webserver.cpp
HEADERS += webserver.h
```

- [ ] **Step 2: Commit**

```bash
git add test/test.pro
git commit -m "build: test.pro 添加 webserver"
```

---

### Task 5: TeaGardenSystem 集成 WebServer

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: teagardensystem.h — 添加成员**

```cpp
#include "webserver.h"
WebServer *webServer = nullptr;
```

- [ ] **Step 2: teagardensystem.cpp — 构造函数初始化**

```cpp
webServer = new WebServer(this);
webServer->start(8080);
connect(webServer, &WebServer::roverCommand, this, [this](char key) {
    // 转发小车控制指令到 LoRa
    loraManager->sendRaw(QByteArray(1, key));
});
```

- [ ] **Step 3: 传感器数据推送**

在 `onLoraData` 中：

```cpp
QJsonObject sensorData;
sensorData["airTemp"] = airTemp;
sensorData["airHumidity"] = airHumidity;
sensorData["lightLux"] = lightLux;
sensorData["soilHumidity"] = soilHumidity;
sensorData["soilTemp"] = soilTemp;
sensorData["soilConductivity"] = soilConductivity;
sensorData["soilPh"] = soilPh;
if (webServer) webServer->pushSensorData(sensorData);
```

- [ ] **Step 4: 视频帧 + YOLO 框推送**

在 `onVideoFrame` 中推无人机帧：

```cpp
QBuffer buf;
buf.open(QIODevice::WriteOnly);
QImage scaled = image.scaled(640, 360, Qt::KeepAspectRatio, Qt::FastTransformation);
scaled.save(&buf, "JPEG", 60);
if (webServer) webServer->pushDroneFrame(buf.data());
```

在 `onYoloResults` 中推 bbox JSON：

```cpp
QJsonArray detections;
for (auto &det : results) {
    QJsonObject d;
    d["class"] = det.className;
    d["score"] = det.score;
    d["x1"] = det.bbox.x();
    d["y1"] = det.bbox.y();
    d["x2"] = det.bbox.x() + det.bbox.width();
    d["y2"] = det.bbox.y() + det.bbox.height();
    detections.append(d);
}
if (webServer) webServer->pushYoloDetections(detections);
```

- [ ] **Step 5: 小车图传帧推送**

在 rover ffmpeg 回调中推小车帧。

- [ ] **Step 6: 天气数据推送**

在 `onNetworkReply` 天气处理成功后推送。

- [ ] **Step 7: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "feat: TeaGardenSystem 集成 WebServer 数据推送"
```

---

### Task 6: 端到端编译验证

- [ ] **Step 1: 桌面端编译**

```bash
make -j$(nproc) 2>&1 | grep -E "error|错误" || echo "编译成功"
```

- [ ] **Step 2: 同步 ELF2 编译**

```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh sync /home/lzy/文档/QTproject/test/
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh compile
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: 网页远程监控完成"
```
