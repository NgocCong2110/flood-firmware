// OWNER: P2
// Wi-Fi, NTP, MQTT, telemetry 1 Hz, status/LWT, nhận lệnh + ACK, đệm và gửi bù, netTask.
// Theo hợp đồng chung v6.1 DRAFT mục 7 (7.1 status, 7.2 id/ACK, 7.3 calibrate, 7.4 scenario, 7.5 NTP).
// TRẠNG THÁI: CHƯA NẠP BOARD. ringbuf.h và netcmd.h đã test trên PC (test/p2_net). net.cpp mới kiểm
// biên dịch với header giả lập trên PC, chưa biên dịch bằng toolchain ESP32.
//
// Khi merge: XOÁ 7 hàm P2 khỏi src/common/stubs.cpp (sampleCommit, netTask, eventPost, ackPost,
// netInit, eventPostState, gNetBeat), nếu không sẽ lỗi trùng định nghĩa.
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "api.h"
#include "ringbuf.h"
#include "netcmd.h"

#if __has_include("secrets.h")
#include "secrets.h"            // src/common/secrets.h, KHÔNG commit (đã có trong .gitignore)
#else
#error "Thieu src/common/secrets.h: chep tu secrets.example.h roi dien WIFI_SSID, WIFI_PASS, MQTT_HOST"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "node01"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef FW_VERSION
#define FW_VERSION "0.2"
#endif
#ifndef NTP_HOST
#define NTP_HOST "pool.ntp.org"      // Wi-Fi không ra Internet thì đặt NTP_HOST = IP máy NTP trong LAN (secrets.h)
#endif
#ifndef NTP_HOST2
#define NTP_HOST2 "time.google.com"
#endif

volatile uint32_t gNetBeat = 0;      // chỉ báo phụ; watchdog thật: netTask tự đăng ký (esp_task_wdt_add)

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static TxRing<900>  gRing;                      // BIẾN TOÀN CỤC (~50 KB), không khai báo trong task
static SemaphoreHandle_t gRingMtx = nullptr;    // khoá riêng của vòng đệm (KHÔNG dùng Lock chung)
static QueueHandle_t gEvQ  = nullptr;           // event: chờ có NTP mới gửi
static QueueHandle_t gAckQ = nullptr;           // ack: không có ts nên gửi ngay khi có MQTT (hợp đồng 7.5)
static uint32_t gSeq = 0;
static volatile bool gPredictive = false;       // chế độ báo trong telemetry, đổi bằng cmd {"mode":...}
static volatile bool gOverflow   = false;       // đã báo BUFFER_OVERFLOW cho đợt tràn này chưa
static volatile int8_t gRssi = 0;
static volatile bool gTimeOk = false;

static char tTel[48], tSta[48], tEvt[48], tAck[48], tCmd[48], tScn[48], tCfg[48];

static const char* RIV_NAME[5] = {"NORMAL", "WATCH", "WARNING", "DANGER", "SENSOR_ERROR"};
static const char* RES_NAME[5] = {"R_OK", "R_WATCH", "R_HIGH", "R_FULL", "R_SENSOR_ERROR"};

struct EvMsg {
  uint8_t  kind;               // 0 = event chung, 1 = STATE_CHANGE
  uint32_t ts, ms;
  float    level;
  char type[20], scope[12], reason[16], from[16], to[16], run_id[12];
};
struct AckMsg {
  bool ok;
  char id[20], reason[16];
};

static uint32_t nowTs() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;     // 0 = chưa có giờ NTP
}

static void ringLock()   { xSemaphoreTake(gRingMtx, portMAX_DELAY); }
static void ringUnlock() { xSemaphoreGive(gRingMtx); }

// ---------------------------------------------------------------- khởi tạo
void netInit() {                               // P1 gọi trong setup(), TRƯỚC khi tạo task
  gRingMtx = xSemaphoreCreateMutex();
  gEvQ  = xQueueCreate(32, sizeof(EvMsg));
  gAckQ = xQueueCreate(16, sizeof(AckMsg));
  snprintf(tTel, sizeof tTel, "flood/%s/telemetry", DEVICE_ID);
  snprintf(tSta, sizeof tSta, "flood/%s/status",    DEVICE_ID);
  snprintf(tEvt, sizeof tEvt, "flood/%s/event",     DEVICE_ID);
  snprintf(tAck, sizeof tAck, "flood/%s/ack",       DEVICE_ID);
  snprintf(tCmd, sizeof tCmd, "flood/%s/cmd",       DEVICE_ID);
  snprintf(tScn, sizeof tScn, "flood/%s/scenario",  DEVICE_ID);
  snprintf(tCfg, sizeof tCfg, "flood/%s/config",    DEVICE_ID);
}

// ---------------------------------------------------------------- event, ack (hàng đợi, an toàn đa task)
// KHÔNG gọi eventPost/eventPostState khi đang giữ Lock: hàm lấy Lock để chép run_id hiện tại.
// P9: phát RUN_STOP TRƯỚC khi xoá gShared.run_id, để event mang run_id của lượt vừa dừng.
static void postEvent(EvMsg& m) {
  if (!gEvQ) return;
  m.ts = nowTs();
  m.ms = millis();
  { Lock l; strlcpy(m.run_id, gShared.run_id, sizeof m.run_id); }
  if (xQueueSend(gEvQ, &m, 0) != pdTRUE) Serial.println("[net] hang doi event day, bo 1 tin");
}

void eventPost(const char* type, const char* reason, const char* scope) {
  EvMsg m; memset(&m, 0, sizeof m);
  m.kind = 0;
  strlcpy(m.type, type ? type : "", sizeof m.type);
  strlcpy(m.reason, reason ? reason : "", sizeof m.reason);
  strlcpy(m.scope, scope ? scope : "", sizeof m.scope);
  postEvent(m);
}

void eventPostState(const char* scope, const char* from, const char* to, float level_mm) {
  EvMsg m; memset(&m, 0, sizeof m);
  m.kind = 1;
  strlcpy(m.type, "STATE_CHANGE", sizeof m.type);
  strlcpy(m.scope, scope ? scope : "", sizeof m.scope);
  strlcpy(m.from, from ? from : "", sizeof m.from);
  strlcpy(m.to, to ? to : "", sizeof m.to);
  m.level = level_mm;
  postEvent(m);
}

void ackPost(const char* id, bool ok, const char* reason) {   // ACK luôn mang đúng id của LỆNH
  if (!gAckQ) return;
  AckMsg m; memset(&m, 0, sizeof m);
  m.ok = ok;
  strlcpy(m.id, id ? id : "", sizeof m.id);
  strlcpy(m.reason, reason ? reason : "", sizeof m.reason);
  if (xQueueSend(gAckQ, &m, 0) != pdTRUE) Serial.println("[net] hang doi ack day, bo 1 tin");
}

static bool publishEvt(EvMsg& m) {
  if (m.ts == 0) m.ts = nowTs() - (millis() - m.ms) / 1000;   // tin tạo lúc chưa có NTP: quy đổi theo millis()
  char b[320];
  int n = snprintf(b, sizeof b, "{\"ts\":%lu,\"type\":\"%s\"", (unsigned long)m.ts, m.type);
  if (m.scope[0])  n += snprintf(b + n, sizeof b - n, ",\"scope\":\"%s\"", m.scope);
  if (m.kind == 1) n += snprintf(b + n, sizeof b - n, ",\"from\":\"%s\",\"to\":\"%s\",\"level_mm\":%.1f",
                                 m.from, m.to, m.level);
  if (m.reason[0]) n += snprintf(b + n, sizeof b - n, ",\"reason\":\"%s\"", m.reason);
  if (m.run_id[0]) n += snprintf(b + n, sizeof b - n, ",\"run_id\":\"%s\"", m.run_id);
  n += snprintf(b + n, sizeof b - n, "}");
  if (n <= 0 || n >= (int)sizeof b) return true;               // không thể xảy ra; bỏ tin thay vì gửi tin cụt
  return mqtt.publish(tEvt, b);
}

static void flushEvents() {                    // chỉ gọi khi đã có NTP
  EvMsg m;
  while (xQueuePeek(gEvQ, &m, 0) == pdTRUE) {
    if (!publishEvt(m)) break;                 // giữ lại, thử vòng sau
    xQueueReceive(gEvQ, &m, 0);
  }
}

static void flushAcks() {                      // ack không có ts: không chờ NTP
  AckMsg m;
  char b[96];
  while (xQueuePeek(gAckQ, &m, 0) == pdTRUE) {
    snprintf(b, sizeof b, "{\"id\":\"%s\",\"ok\":%s,\"reason\":\"%s\"}", m.id, m.ok ? "true" : "false", m.reason);
    if (!mqtt.publish(tAck, b)) break;
    xQueueReceive(gAckQ, &m, 0);
  }
}

// ---------------------------------------------------------------- telemetry
void sampleCommit() {                          // task sensor (P1) gọi mỗi giây, NGOÀI Lock
  if (!gRingMtx) return;
  Shared s = sharedCopy();
  Sample x; memset(&x, 0, sizeof x);
  x.seq = ++gSeq;
  x.boot = boot_count;
  x.ts = nowTs();
  x.ms = millis();
  x.raw_mm = s.riv.raw_mm;
  x.level_mm = s.riv.level_mm;
  x.res_mm = s.res.level_mm;
  x.rssi = gRssi;
  x.pump_tide_pct = (uint8_t)constrain(s.pump_tide_pct, 0, 100);
  x.riv_state = s.riv_err ? 4 : (uint8_t)constrain(s.riv_lvl, 0, 3);
  x.res_state = s.res_err ? 4 : (uint8_t)constrain(s.res_lvl, 0, 3);
  if (s.riv.valid)       x.flags |= F_VALID;
  if (s.res.valid)       x.flags |= F_RES_VALID;
  if (s.pump_on)         x.flags |= F_PUMP_ON;
  if (s.reservoir_full)  x.flags |= F_RES_FULL;        // v6: siêu âm hồ hợp lệ và >= R_full
  if (s.res_float_cut)   x.flags |= F_RES_FLOAT_CUT;   // v6: GPIO32 == LOW
  if (s.river_overflow)  x.flags |= F_RIV_OVER;        // GPIO19 == LOW
  if (gPredictive)       x.flags |= F_PREDICTIVE;
  if (!(s.mqtt_ok && x.ts)) x.flags |= F_REPLAY;       // đo lúc không gửi ngay được -> khi gửi là bản bù
  strlcpy(x.run_id, s.run_id, sizeof x.run_id);

  ringLock();
  bool lost = gRing.push(x);
  ringUnlock();
  if (lost && !gOverflow) { gOverflow = true; eventPost("BUFFER_OVERFLOW", "", ""); }   // báo MỘT lần mỗi đợt
}

static bool sendTel(const Sample& x, bool replay) {
  char b[512];                                 // tối đa ~400 byte; setBufferSize(768)
  int n = snprintf(b, sizeof b,
    "{\"seq\":%lu,\"boot\":%lu,\"replay\":%s,\"ts\":%lu,\"raw_mm\":%.1f,\"level_mm\":%.1f,"
    "\"valid\":%s,\"state\":\"%s\",\"res_mm\":%.1f,\"res_valid\":%s,\"res_state\":\"%s\","
    "\"pump_drain\":\"%s\",\"pump_tide_pct\":%u,\"reservoir_full\":%s,\"res_float_cut\":%s,\"river_overflow\":%s,"
    "\"mode\":\"%s\",\"run_id\":\"%s\",\"rssi\":%d,\"source\":\"real\"}",
    (unsigned long)x.seq, (unsigned long)x.boot, replay ? "true" : "false", (unsigned long)x.ts,
    x.raw_mm, x.level_mm,
    (x.flags & F_VALID) ? "true" : "false", RIV_NAME[x.riv_state > 4 ? 4 : x.riv_state],
    x.res_mm, (x.flags & F_RES_VALID) ? "true" : "false", RES_NAME[x.res_state > 4 ? 4 : x.res_state],
    (x.flags & F_PUMP_ON) ? "on" : "off", (unsigned)x.pump_tide_pct,
    (x.flags & F_RES_FULL) ? "true" : "false", (x.flags & F_RES_FLOAT_CUT) ? "true" : "false",
    (x.flags & F_RIV_OVER) ? "true" : "false",
    (x.flags & F_PREDICTIVE) ? "predictive" : "reactive", x.run_id, (int)x.rssi);
  if (n <= 0 || n >= (int)sizeof b) return false;   // cắt cụt thì không gửi
  return mqtt.publish(tTel, b);
}

static void flushTelemetry() {                 // chỉ gọi khi MQTT nối và đã có NTP
  uint32_t now = nowTs();
  ringLock();
  gRing.fixTimestamps(now, millis());          // bản đo trước khi có NTP: quy đổi ts
  ringUnlock();

  for (int i = 0; i < 5; i++) {                // tối đa 5 bản mỗi vòng 20 ms
    Sample x; bool have;
    ringLock(); have = gRing.peekUnsent(x); ringUnlock();
    if (!have) break;
    bool rp = (x.flags & F_REPLAY) || (now > x.ts + 5);   // gửi trễ > 5 s so với lúc đo cũng là bản bù
    if (!sendTel(x, rp)) break;                // gửi lỗi: giữ lại, thử vòng sau
    ringLock(); gRing.markSent(x.boot, x.seq); ringUnlock();
  }
  ringLock();
  gRing.purge(now);                            // xoá bản đã gửi quá 60 s
  if (gRing.unsent() < gRing.capacity() / 2) gOverflow = false;
  ringUnlock();
}

static void onLinkLost() {
  uint32_t now = nowTs();
  if (!now) return;                            // chưa có giờ thì chưa gửi bản nào
  ringLock(); gRing.onLinkLost(now); ringUnlock();
  Serial.println("[net] mat lien ket: danh dau gui lai 40 s gan nhat");
}

// ---------------------------------------------------------------- nhận lệnh
static void handleCmd(JsonVariantConst doc) {
  using namespace netcmd;
  Decoded d = decodeCmd(doc, cfgGet().cmd_ttl_s);
  switch (d.act) {
    case Act::Pump:                            // P7 tự ACK (kể cả khi bị chặn hoặc giá trị sai)
      pumpOnCommand(d.id, d.pump_val, d.ttl_s);
      return;
    case Act::Mode:
      gPredictive = d.predictive;
      ackPost(d.id, true, "");
      return;
    case Act::Calibrate: {
      const char* why = "invalid_cmd";
      bool ok = calibrateApply(d.sensor, d.a, d.b, &why);   // P1 kiểm lại sensor và khoảng a, b
      ackPost(d.id, ok, ok ? "" : (why && why[0] ? why : "invalid_cmd"));
      return;
    }
    default:
      if (!d.id[0]) Serial.println("[net] cmd thieu hoac sai id");
      ackPost(d.id, false, "invalid_cmd");
      return;
  }
}

static void handleScenario(JsonVariantConst doc) {
  using namespace netcmd;
  Decoded d = decodeScenario(doc);
  const char* why = "";
  bool ok = false;
  if (d.act == Act::ScnStop) {
    ok = scenarioStop(d.run_id, &why);         // P9: run_id khác lượt đang chạy vẫn dừng (v6.1)
  } else if (d.act == Act::ScnStart) {
    ScenarioReq r{};
    r.start = true;
    strlcpy(r.run_id, d.run_id, sizeof r.run_id);
    r.period_s = d.period_s; r.P0 = d.P0; r.P1 = d.P1;
    r.has_surge = d.has_surge; r.s_t = d.s_t; r.s_amp = d.s_amp; r.s_w = d.s_w;
    ok = scenarioStart(r, &why);               // P9: why = "" | "clamped" (ok) | "invalid_cmd"
  } else {
    if (!d.id[0]) Serial.println("[net] scenario thieu hoac sai id");
    ackPost(d.id, false, "invalid_cmd");
    return;
  }
  if (!why) why = "";
  if (!ok && !why[0]) why = "invalid_cmd";
  ackPost(d.id, ok, why);                      // id của LỆNH, không bao giờ là run_id
}

static void handleConfig(JsonVariantConst doc) {   // retain: tới ngay mỗi lần subscribe
  Config c = cfgGet();
  if (!netcmd::applyConfig(doc, c)) { Serial.println("[net] config khong hop le, bo qua"); return; }
  cfgSet(c);                                   // P1: cfgSet chỉ ghi NVS khi có thay đổi thật
}

static void onMsg(char* topic, byte* payload, unsigned int len) {
  bool isCmd = !strcmp(topic, tCmd), isScn = !strcmp(topic, tScn), isCfg = !strcmp(topic, tCfg);
  if (!isCmd && !isScn && !isCfg) return;
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) {
    Serial.printf("[net] JSON hong tren %s\n", topic);
    if (isCmd || isScn) ackPost("", false, "invalid_cmd");   // không đọc được id
    return;
  }
  if (isCmd) handleCmd(doc.as<JsonVariantConst>());
  else if (isScn) handleScenario(doc.as<JsonVariantConst>());
  else handleConfig(doc.as<JsonVariantConst>());
}

// ---------------------------------------------------------------- kết nối
static void publishStatus() {                  // cùng schema JSON với LWT; backend đọc trường "state"
  char b[96];
  snprintf(b, sizeof b, "{\"state\":\"online\",\"fw\":\"%s\",\"uptime\":%lu}", FW_VERSION,
           (unsigned long)(millis() / 1000));
  mqtt.publish(tSta, b, true);                 // retain; PubSubClient chỉ publish QoS 0 (hợp đồng 15 #14)
}

static char gLwt[64];

static bool mqttConnect() {
  char cid[40];
  snprintf(cid, sizeof cid, "esp32-%s-%04x", DEVICE_ID, (unsigned)(ESP.getEfuseMac() & 0xFFFF));
  snprintf(gLwt, sizeof gLwt, "{\"state\":\"offline\",\"fw\":\"%s\"}", FW_VERSION);
#if defined(MQTT_USER) && defined(MQTT_PASS)
  bool ok = mqtt.connect(cid, MQTT_USER, MQTT_PASS, tSta, 1, true, gLwt);
#else
  bool ok = mqtt.connect(cid, nullptr, nullptr, tSta, 1, true, gLwt);   // LWT QoS 1, retain
#endif
  if (!ok) { Serial.printf("[net] MQTT that bai, state=%d\n", mqtt.state()); return false; }
  mqtt.subscribe(tCmd);
  mqtt.subscribe(tScn);
  mqtt.subscribe(tCfg);
  publishStatus();
  Serial.println("[net] MQTT da noi");
  return true;
}

void netTask(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(768);                     // mặc định 256 sẽ làm publish telemetry thất bại âm thầm
  mqtt.setKeepAlive(15);                       // TCP "nửa chết" bị phát hiện sau ~20 s (< 40 s cửa sổ gửi lại)
  mqtt.setSocketTimeout(3);                    // connect không treo quá 3 s
  mqtt.setCallback(onMsg);

  esp_task_wdt_add(NULL);                      // watchdog 30 s (P1 đã init); mất mạng KHÔNG làm treo vòng lặp
  bool ntpStarted = false, wasUp = false, timeLogged = false;
  uint32_t nextMqtt = 0, backoff = 500, lastWifiTry = millis(), lastStatus = 0;

  for (;;) {
    gNetBeat = millis();
    esp_task_wdt_reset();

    bool wifi = (WiFi.status() == WL_CONNECTED);
    if (wifi) lastWifiTry = millis();
    else if (millis() - lastWifiTry > 15000) { WiFi.reconnect(); lastWifiTry = millis(); }
    if (wifi && !ntpStarted) { configTime(0, 0, NTP_HOST, NTP_HOST2); ntpStarted = true; }

    gTimeOk = (nowTs() != 0);
    if (gTimeOk && !timeLogged) { Serial.printf("[net] da co NTP, ts=%lu\n", (unsigned long)nowTs()); timeLogged = true; }
    gRssi = wifi ? (int8_t)WiFi.RSSI() : 0;

    bool up = wifi && mqtt.connected();
    if (wasUp && !up) onLinkLost();
    wasUp = up;

    if (wifi && !mqtt.connected() && (int32_t)(millis() - nextMqtt) >= 0) {
      if (mqttConnect()) { backoff = 500; lastStatus = millis(); }
      else backoff = backoff * 2 > 5000 ? 5000 : backoff * 2;   // 0,5 / 1 / 2 / 4 / 5 s: phục hồi ≤ 15 s (#10)
      nextMqtt = millis() + backoff;
    }

    bool connected = mqtt.connected();
    { Lock l; gShared.mqtt_ok = connected; }   // P7 (ui.cpp) dùng để nháy LED mất mạng

    if (connected) {
      mqtt.loop();                             // nhận lệnh -> onMsg
      flushAcks();                             // ack không chờ NTP
      if (gTimeOk) { flushEvents(); flushTelemetry(); }   // chưa có NTP thì chưa phát telemetry/event (#9)
      if (millis() - lastStatus > 30000) { publishStatus(); lastStatus = millis(); }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
