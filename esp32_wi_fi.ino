#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ========== 改成你自己的 Wi-Fi（和笔记本同一个路由器）==========
const char* WIFI_SSID     = "zizai3344";
const char* WIFI_PASSWORD = "4433iaziz";
// ==============================================================

WebServer server(80);

struct ApInfo {
  char ssid[33];
  char bssid[18];
  int32_t rssi;
  uint8_t channel;
  char auth[16];
};

static const int MAX_APS = 32;
ApInfo apList[MAX_APS];
int apCount = 0;

bool scanInProgress = false;
unsigned long lastScanStartMs = 0;
const unsigned long SCAN_INTERVAL_MS = 60000;

const char* getAuthModeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN: return "开放";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "企业级";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    default: return "未知";
  }
}

void htmlEscapeTo(String& out, const char* s) {
  if (!s) return;
  for (const char* p = s; *p; p++) {
    if (*p == '&') out += "&amp;";
    else if (*p == '<') out += "&lt;";
    else if (*p == '>') out += "&gt;";
    else if (*p == '"') out += "&quot;";
    else out += *p;
  }
}

void formatMac(char* buf, size_t buflen, const uint8_t* bssid) {
  if (!bssid) {
    snprintf(buf, buflen, "--");
    return;
  }
  snprintf(buf, buflen, "%02X:%02X:%02X:%02X:%02X:%02X",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

int countSameSsid(const char* ssid) {
  if (!ssid || !ssid[0]) return 1;
  int c = 0;
  for (int i = 0; i < apCount; i++) {
    if (strcmp(apList[i].ssid, ssid) == 0) c++;
  }
  return c;
}

void updateScanCache(int n) {
  if (n < 0) n = 0;
  if (n > MAX_APS) n = MAX_APS;

  int idx[MAX_APS];
  for (int i = 0; i < n; i++) idx[i] = i;
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i])) {
        int t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
      }
    }
  }

  apCount = n;
  for (int k = 0; k < n; k++) {
    int i = idx[k];
    String ssid = WiFi.SSID(i);
    strncpy(apList[k].ssid, ssid.c_str(), 32);
    apList[k].ssid[32] = '\0';
    formatMac(apList[k].bssid, sizeof(apList[k].bssid), WiFi.BSSID(i));
    apList[k].rssi = WiFi.RSSI(i);
    apList[k].channel = WiFi.channel(i);
    strncpy(apList[k].auth, getAuthModeName(WiFi.encryptionType(i)), sizeof(apList[k].auth) - 1);
    apList[k].auth[sizeof(apList[k].auth) - 1] = '\0';
  }

  Serial.printf("扫描完成，发现 %d 个热点，空闲内存 %u\n", apCount, ESP.getFreeHeap());
}

void handleApiScan() {
  String json;
  json.reserve(apCount * 96 + 8);
  json += '[';
  for (int i = 0; i < apCount; i++) {
    if (i) json += ',';
    json += "{\"ssid\":\"";
    // JSON 简单转义
    for (const char* p = apList[i].ssid; *p; p++) {
      if (*p == '\\' || *p == '"') json += '\\';
      json += *p;
    }
    json += "\",\"bssid\":\"";
    json += apList[i].bssid;
    json += "\",\"rssi\":";
    json += String(apList[i].rssi);
    json += ",\"channel\":";
    json += String(apList[i].channel);
    json += ",\"authmode\":\"";
    json += apList[i].auth;
    json += "\"}";
  }
  json += ']';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

void handleRoot() {
  int channelCounts[13] = {0};
  for (int i = 0; i < apCount; i++) {
    uint8_t ch = apList[i].channel;
    if (ch >= 1 && ch <= 13) channelCounts[ch - 1]++;
  }
  int maxCount = 1;
  for (int i = 0; i < 13; i++) {
    if (channelCounts[i] > maxCount) maxCount = channelCounts[i];
  }

  String html;
  html.reserve(9000);
  html += F("<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
  html += F("<meta http-equiv=\"refresh\" content=\"60\">");
  html += F("<title>ESP32 周边 Wi-Fi 一览</title><style>");
  html += F("*{box-sizing:border-box}body{margin:0;padding:20px;font-family:Microsoft YaHei,Segoe UI,sans-serif;");
  html += F("background:#020617;color:#e2e8f0;min-height:100vh}");
  html += F("#mx{position:fixed;inset:0;z-index:0;opacity:.2;pointer-events:none}");
  html += F(".wrap{position:relative;z-index:1;max-width:1100px;margin:0 auto}");
  html += F("h1{text-align:center;color:#4ade80;margin:0 0 14px;font-size:1.4rem;text-shadow:0 0 12px rgba(74,222,128,.35)}");
  html += F(".me{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;");
  html += F("background:rgba(15,23,42,.85);border:1px solid #14532d;padding:14px;border-radius:12px;margin-bottom:16px}");
  html += F(".me div{font-size:.82rem;color:#94a3b8}.me b{display:block;color:#f8fafc;font-size:.95rem;margin-top:2px}");
  html += F(".card{background:rgba(15,23,42,.9);border:1px solid #1e293b;padding:16px;border-radius:12px;margin-bottom:16px}");
  html += F(".title{color:#86efac;font-weight:700;margin-bottom:12px;border-bottom:1px solid #334155;padding-bottom:8px}");
  html += F(".chans{display:flex;align-items:flex-end;gap:6px;height:140px}.chan{flex:1;text-align:center}");
  html += F(".bar{background:linear-gradient(180deg,#4ade80,#166534);border-radius:4px 4px 0 0;min-height:2px;margin:0 auto;width:70%}");
  html += F(".chan span{display:block;color:#94a3b8;font-size:11px;margin-top:4px}");
  html += F("table{width:100%;border-collapse:collapse}th,td{padding:9px 8px;border-bottom:1px solid #334155;font-size:.88rem;text-align:left}");
  html += F("th{color:#4ade80}.mac{font-family:Consolas,monospace;font-size:.78rem;color:#94a3b8}");
  html += F(".tag{display:inline-block;margin-left:6px;padding:1px 6px;border-radius:4px;font-size:.72rem;background:#3b82f6;color:#fff}");
  html += F(".open{background:rgba(34,197,94,.12)}.badge{display:inline-block;margin-left:6px;padding:1px 6px;border-radius:4px;font-size:.72rem;background:#16a34a;color:#fff}");
  html += F(".rssi{height:8px;border-radius:4px;background:#334155;overflow:hidden;margin-top:4px;max-width:120px}");
  html += F(".rssi>i{display:block;height:100%;background:linear-gradient(90deg,#ef4444,#eab308,#22c55e)}");
  html += F(".status{text-align:center;color:#64748b;font-size:.85rem}a{color:#4ade80}</style></head><body>");
  html += F("<canvas id=\"mx\"></canvas><div class=\"wrap\"><h1>ESP32 周边 Wi-Fi 一览</h1><div class=\"me\">");

  html += F("<div>已连接网络<b>");
  htmlEscapeTo(html, WIFI_SSID);
  html += F("</b></div><div>本机 IP<b>");
  html += WiFi.localIP().toString();
  html += F("</b></div><div>网关<b>");
  html += WiFi.gatewayIP().toString();
  html += F("</b></div><div>当前信道<b>");
  html += String(WiFi.channel());
  html += F("</b></div><div>到路由器信号<b>");
  html += String(WiFi.RSSI());
  html += F(" dBm</b></div><div>周边热点数<b>");
  html += String(apCount);
  html += F("</b></div></div>");

  html += F("<div class=\"card\"><div class=\"title\">2.4GHz 信道占用（1-13）</div><div class=\"chans\">");
  for (int i = 0; i < 13; i++) {
    int h = (int)(120.0f * channelCounts[i] / maxCount);
    if (channelCounts[i] > 0 && h < 8) h = 8;
    html += F("<div class=\"chan\"><div class=\"bar\" style=\"height:");
    html += String(h);
    html += F("px\"></div><span>");
    html += String(i + 1);
    html += F("信道<br>");
    html += String(channelCounts[i]);
    html += F("个</span></div>");
  }
  html += F("</div></div><div class=\"card\"><div class=\"title\">周边热点明细（按信号从强到弱）</div>");
  html += F("<div style=\"overflow-x:auto\"><table><thead><tr>");
  html += F("<th>名称 SSID</th><th>BSSID (MAC)</th><th>信道</th><th>信号</th><th>加密</th>");
  html += F("</tr></thead><tbody>");

  if (apCount == 0) {
    html += F("<tr><td colspan=\"5\" style=\"text-align:center\">正在扫描，约一分钟内刷新可见结果</td></tr>");
  } else {
    for (int i = 0; i < apCount; i++) {
      bool isOpen = (strcmp(apList[i].auth, "开放") == 0);
      int dup = countSameSsid(apList[i].ssid);
      int percent = 2 * ((int)apList[i].rssi + 100);
      if (percent < 0) percent = 0;
      if (percent > 100) percent = 100;

      html += F("<tr");
      if (isOpen) html += F(" class=\"open\"");
      html += F("><td><strong>");
      if (apList[i].ssid[0]) htmlEscapeTo(html, apList[i].ssid);
      else html += F("<em>[隐藏名称]</em>");
      html += F("</strong>");
      if (apList[i].ssid[0] && dup > 1) {
        html += F("<span class=\"tag\">同名 ");
        html += String(dup);
        html += F(" 台</span>");
      }
      if (isOpen) html += F("<span class=\"badge\">未加密</span>");
      html += F("</td><td class=\"mac\">");
      html += apList[i].bssid;
      html += F("</td><td>");
      html += String(apList[i].channel);
      html += F("</td><td>");
      html += String(apList[i].rssi);
      html += F(" dBm<div class=\"rssi\"><i style=\"width:");
      html += String(percent);
      html += F("%\"></i></div></td><td>");
      htmlEscapeTo(html, apList[i].auth);
      html += F("</td></tr>");
    }
  }

  html += F("</tbody></table></div></div>");
  html += F("<div class=\"status\">每 60 秒自动扫描并刷新 · <a href=\"/api/scan\">/api/scan</a></div></div>");
  // 轻量代码雨：仅浏览器运行
  html += F("<script>(function(){var c=document.getElementById('mx');if(!c||!c.getContext)return;");
  html += F("var g=c.getContext('2d'),w,h,cols,d,ch='01ABCDEFアイウエオ';");
  html += F("function rz(){w=c.width=innerWidth;h=c.height=innerHeight;cols=Math.max(8,(w/20)|0);d=[];for(var i=0;i<cols;i++)d[i]=Math.random()*-40}");
  html += F("addEventListener('resize',rz);rz();function tick(){if(document.hidden){setTimeout(tick,800);return}");
  html += F("g.fillStyle='rgba(2,6,23,.14)';g.fillRect(0,0,w,h);g.fillStyle='#22c55e';g.font='13px monospace';");
  html += F("for(var i=0;i<cols;i++){g.fillText(ch[(Math.random()*ch.length)|0],i*20,d[i]*16);");
  html += F("if(d[i]*16>h&&Math.random()>.975)d[i]=0;else d[i]++}setTimeout(tick,50)}tick()})();</script>");
  html += F("</body></html>");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("Wi-Fi 断开，尝试重连...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    Serial.print(".");
    server.handleClient();  // 重连时也尽量处理网页请求
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\n重连成功" : "\n重连失败");
}

void startWebServer() {
  server.stop();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/scan", HTTP_GET, handleApiScan);
  server.begin();
  Serial.println("网页服务已启动/重启");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // 关闭休眠，扫描后网页端口更稳定
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("esp32-wifi-analyzer");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.print("正在连接 Wi-Fi: ");
  Serial.println(WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - start > 30000) {
      Serial.println("\n连接超时，请检查 SSID/密码。");
      return;
    }
  }

  Serial.println("\nWi-Fi 已连接!");
  Serial.print("打开: http://");
  Serial.println(WiFi.localIP());
  Serial.printf("空闲内存: %u\n", ESP.getFreeHeap());

  if (MDNS.begin("esp32-wifi")) {
    Serial.println("也可试: http://esp32-wifi.local/");
  }

  startWebServer();

  WiFi.scanNetworks(true, true);
  scanInProgress = true;
  lastScanStartMs = millis();
  Serial.println("已开始后台扫描...");
}

void loop() {
  server.handleClient();

  if (scanInProgress) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      updateScanCache(n);
      WiFi.scanDelete();
      scanInProgress = false;
      lastScanStartMs = millis();

      // 扫描常会打断 TCP，扫完后确认联网并重启网页服务
      ensureWifiConnected();
      startWebServer();
      Serial.printf("扫后状态 WiFi=%d 空闲内存=%u\n", WiFi.status(), ESP.getFreeHeap());
    } else if (n == WIFI_SCAN_FAILED) {
      Serial.println("扫描失败，稍后重试");
      WiFi.scanDelete();
      scanInProgress = false;
      lastScanStartMs = millis();
      ensureWifiConnected();
      startWebServer();
    }
  } else if (millis() - lastScanStartMs >= SCAN_INTERVAL_MS) {
    ensureWifiConnected();
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.scanNetworks(true, true);
      scanInProgress = true;
      lastScanStartMs = millis();
      Serial.println("开始新一轮扫描...");
    }
  }
}
