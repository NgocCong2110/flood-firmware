#pragma once
// OWNER: P2
// Kiểm cấu trúc lệnh MQTT (cmd, scenario, config). Chỉ dùng ArduinoJson, KHÔNG dùng Arduino/FreeRTOS,
// nên test được trên PC (test/p2_net/test_p2.cpp). net.cpp gọi các hàm này rồi mới gọi P1/P7/P9.
// Hợp đồng v6.1 mục 7.2 (id/ACK), 7.3 (calibrate), 7.4 (scenario); quyết định #26, #28.
//
// Phân vai: P2 kiểm CẤU TRÚC (JSON, id, run_id, có đủ trường, đúng kiểu). Kiểm GIÁ TRỊ là việc của
// P1 (a, b), P7 (bật/tắt bơm), P9 (period_s, P0, P1, surge, lượt đang chạy).
#include <ArduinoJson.h>
#include <string.h>
#include <stdint.h>

namespace netcmd {

// Chuỗi 1..maxLen ký tự, chỉ [A-Za-z0-9_-]. Không cắt cụt (cắt sẽ làm ACK sai id).
// id: maxLen 19 (bộ đệm id[20]); run_id: maxLen 11 (bộ đệm run_id[12]).
inline bool validToken(const char* s, size_t maxLen) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n < 1 || n > maxLen) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

inline const char* strField(JsonVariantConst v) { return v.is<const char*>() ? v.as<const char*>() : nullptr; }
inline bool isNum(JsonVariantConst v) { return v.is<float>() && !v.is<bool>(); }

enum class Act : uint8_t {
  Reject,      // ACK ok=false, reason=invalid_cmd, không làm gì
  Pump,        // chuyển cho P7 pumpOnCommand(id, pump_val, ttl) — P7 tự ACK
  Mode,        // P2 đổi chế độ và ACK ok
  Calibrate,   // gọi P1 calibrateApply(sensor, a, b, &why) rồi P2 ACK
  ScnStart,    // gọi P9 scenarioStart(req, &why) rồi P2 ACK
  ScnStop      // gọi P9 scenarioStop(run_id, &why) rồi P2 ACK
};

struct Decoded {
  Act  act = Act::Reject;
  char id[20] = "";             // rỗng nếu không đọc được id hợp lệ
  // Pump
  char pump_val[8] = "";        // "on"/"off" (P7 kiểm giá trị; "force" chỉ ở build debug)
  int  ttl_s = 0;
  // Mode
  bool predictive = false;
  // Calibrate
  char  sensor[12] = "";        // "river" | "reservoir"
  float a = 0, b = 0;
  // Scenario
  char  run_id[12] = "";
  float period_s = 0, P0 = 0, P1 = 0;
  bool  has_surge = false;
  float s_t = 0, s_amp = 0, s_w = 0;
};

// Lấy id; false nếu thiếu/sai (ACK sẽ mang id "").
inline bool takeId(JsonVariantConst root, Decoded& d) {
  const char* id = strField(root["id"]);
  if (!validToken(id, 19)) return false;
  strncpy(d.id, id, sizeof d.id - 1);
  return true;
}

// Topic cmd. default_ttl lấy từ config cmd_ttl_s.
inline Decoded decodeCmd(JsonVariantConst root, int default_ttl) {
  Decoded d;
  if (!root.is<JsonObjectConst>() || !takeId(root, d)) return d;      // Reject, id ""

  int actions = (root["pump_drain"].isNull() ? 0 : 1) + (root["mode"].isNull() ? 0 : 1) +
                (root["calibrate"].isNull() ? 0 : 1);
  if (actions != 1) return d;                                          // đúng MỘT hành động mỗi lệnh

  if (!root["pump_drain"].isNull()) {
    const char* v = strField(root["pump_drain"]);
    if (!v || strlen(v) >= sizeof d.pump_val) return d;
    int ttl = default_ttl;
    if (!root["ttl_s"].isNull()) {
      if (!isNum(root["ttl_s"])) return d;
      ttl = (int)root["ttl_s"].as<float>();
    }
    if (ttl < 1) ttl = 1;
    if (ttl > 300) ttl = 300;                                          // hợp đồng: tối đa 300
    strncpy(d.pump_val, v, sizeof d.pump_val - 1);
    d.ttl_s = ttl;
    d.act = Act::Pump;
    return d;
  }
  if (!root["mode"].isNull()) {
    const char* m = strField(root["mode"]);
    if (!m) return d;
    if (!strcmp(m, "reactive"))        d.predictive = false;
    else if (!strcmp(m, "predictive")) d.predictive = true;
    else return d;
    d.act = Act::Mode;
    return d;
  }
  // calibrate: "sensor" nằm TRONG object calibrate, bắt buộc, chỉ "river"/"reservoir"; không mặc định.
  JsonVariantConst c = root["calibrate"];
  if (!c.is<JsonObjectConst>()) return d;
  const char* sn = strField(c["sensor"]);
  if (!sn || (strcmp(sn, "river") && strcmp(sn, "reservoir"))) return d;
  if (!isNum(c["a"]) || !isNum(c["b"])) return d;
  strncpy(d.sensor, sn, sizeof d.sensor - 1);
  d.a = c["a"].as<float>();
  d.b = c["b"].as<float>();
  d.act = Act::Calibrate;
  return d;
}

// Topic scenario.
// v6.1: start và stop đều bắt buộc id, scenario, run_id. P2 KHÔNG so run_id với lượt đang chạy (việc của P9;
// stop có run_id khác lượt đang chạy thì P9 vẫn dừng).
inline Decoded decodeScenario(JsonVariantConst root) {
  Decoded d;
  if (!root.is<JsonObjectConst>() || !takeId(root, d)) return d;
  const char* sc  = strField(root["scenario"]);
  const char* rid = strField(root["run_id"]);
  if (!sc || !validToken(rid, 11)) return d;
  strncpy(d.run_id, rid, sizeof d.run_id - 1);

  if (!strcmp(sc, "stop")) { d.act = Act::ScnStop; return d; }
  if (strcmp(sc, "start")) return d;

  if (!isNum(root["period_s"]) || !isNum(root["P0"]) || !isNum(root["P1"])) return d;
  d.period_s = root["period_s"].as<float>();
  d.P0 = root["P0"].as<float>();
  d.P1 = root["P1"].as<float>();
  if (!root["surge"].isNull()) {                 // tuỳ chọn, nhưng đã có thì phải đủ và đúng kiểu
    JsonVariantConst su = root["surge"];
    if (!su.is<JsonObjectConst>() || !isNum(su["t"]) || !isNum(su["amp"]) || !isNum(su["width"])) return d;
    d.has_surge = true;
    d.s_t = su["t"].as<float>(); d.s_amp = su["amp"].as<float>(); d.s_w = su["width"].as<float>();
  }
  d.act = Act::ScnStart;
  return d;
}

// Topic config: chỉ ghi đè trường có mặt và đúng kiểu số. Trả false nếu kết quả không hợp lý (bỏ qua, giữ cũ).
// Dùng template để test trên PC không cần api.h.
template <class Cfg>
inline bool applyConfig(JsonVariantConst root, Cfg& c) {
  if (!root.is<JsonObjectConst>()) return false;
  Cfg n = c;
#define P2_F(k) if (isNum(root[#k])) n.k = root[#k].template as<decltype(n.k)>();
  P2_F(L1) P2_F(L2) P2_F(L3) P2_F(hyst) P2_F(pump_off) P2_F(H_max)
  P2_F(R_watch) P2_F(R_high) P2_F(R_full) P2_F(jump_mm)
  P2_F(pwm_min) P2_F(pwm_max) P2_F(cmd_ttl_s) P2_F(r2_min)
  P2_F(pre_alert_s) P2_F(pre_drain_s) P2_F(fc_stale_s)
#undef P2_F
  bool sane = n.L1 < n.L2 && n.L2 < n.L3 && n.L3 <= n.H_max && n.hyst >= 0 &&
              n.R_watch < n.R_high && n.R_high < n.R_full &&
              n.pump_off > 0 && n.pump_off < n.L3 && n.jump_mm > 0 &&
              n.pwm_min >= 0 && n.pwm_min <= n.pwm_max && n.pwm_max <= 100 &&
              n.cmd_ttl_s >= 1 && n.cmd_ttl_s <= 300;
  if (!sane) return false;
  c = n;
  return true;
}

}  // namespace netcmd
