// OWNER: P9
// Kịch bản bơm triều B: scenarioStart/Stop/Task, kẹp pwm_max.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.
#include <Arduino.h>
#include "api.h"
#include "pins.h"
#include "scenario_calc.h"

static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static bool gRunning = false;
static ScenarioParams gP;
static uint32_t gT0 = 0;
static uint32_t gAutoSeq = 0;
static char gRunId[12] = "";

// Dừng không điều kiện. PWM = 0 ngay.
static void stopRun() {
  bool was;
  portENTER_CRITICAL(&gMux); was = gRunning; gRunning = false; portEXIT_CRITICAL(&gMux);
  ledcWrite(CH_TIDE, 0);
  if (was) {
    eventPost("RUN_STOP", "", "");                  // run_id còn nguyên -> event mang đúng run_id
    Lock l; gShared.run_id[0] = 0; gShared.pump_tide_pct = 0;
  }
}

bool scenarioStart(const ScenarioReq& r, const char** why) {
  Config c = cfgGet();
  ScenarioParams p{r.period_s, r.P0, r.P1, r.has_surge, r.s_t, r.s_amp, r.s_w};
  *why = "";
  if (!r.start || !scenarioValid(p, c.pwm_min)) { *why = "invalid_cmd"; return false; }
  char rid[12];
  strlcpy(rid, r.run_id, sizeof rid);
  if (!rid[0]) {                                    // nhánh phòng hờ (tạm)
    unsigned b; { Lock l; b = (unsigned)(gShared.boot % 1000); }
    snprintf(rid, sizeof rid, "r%u-%u", b, (unsigned)(++gAutoSeq % 1000));
  }
  if (r.P0 + r.P1 > c.pwm_max) *why = "clamped";
  stopRun();                                        // chạy đè: dừng lượt cũ nếu có
  { Lock l; strlcpy(gShared.run_id, rid, sizeof gShared.run_id); }   // đặt TRƯỚC RUN_START
  portENTER_CRITICAL(&gMux);
  gP = p; gT0 = millis(); strlcpy(gRunId, rid, sizeof gRunId); gRunning = true;
  portEXIT_CRITICAL(&gMux);
  eventPost("RUN_START", "", "");
  return true;
}

bool scenarioStop(const char* run_id, const char** why) {
  (void)run_id;                                     // tạm: không bắt buộc khớp
  bool running;
  portENTER_CRITICAL(&gMux); running = gRunning; portEXIT_CRITICAL(&gMux);
  if (!running) { *why = "invalid_cmd"; return false; }
  stopRun();
  *why = "";
  return true;
}

void scenarioTask(void*) {
  ledcSetup(CH_TIDE, 1000, 8);                      // core 2.x. (core 3.x: ledcAttach)
  ledcAttachPin(pin::PUMP_TIDE, CH_TIDE);
  ledcWrite(CH_TIDE, 0);
  TickType_t tick = xTaskGetTickCount();
  for (;;) {
    bool run; ScenarioParams p; uint32_t t0;
    portENTER_CRITICAL(&gMux); run = gRunning; p = gP; t0 = gT0; portEXIT_CRITICAL(&gMux);
    uint8_t pct = 0;
    if (run) {
      Config c = cfgGet();                          // đọc pwm_max mỗi vòng
      float t = (millis() - t0) / 1000.0f;
      pct = (uint8_t)lroundf(scenarioPwm(p, t, c.pwm_max));
    }
    ledcWrite(CH_TIDE, (pct * 255 + 50) / 100);
    { Lock l; gShared.pump_tide_pct = pct; }
    bool still; portENTER_CRITICAL(&gMux); still = gRunning; portEXIT_CRITICAL(&gMux);
    if (!still && pct) { ledcWrite(CH_TIDE, 0); Lock l; gShared.pump_tide_pct = 0; }
    vTaskDelayUntil(&tick, pdMS_TO_TICKS(100));
  }
}