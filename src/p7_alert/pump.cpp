// OWNER: P7
// Bơm xả A: REACTIVE, lệnh backend, chặn an toàn, TTL.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.

#include <Arduino.h>
#include "api.h"
#include "pins.h"
#include "pumplogic.h"

static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static PumpLogic gLogic;
static float gLastLevel = 0;     // D15: mực sông hợp lệ gần nhất (0 = chưa có → bơm tắt)
#ifdef FW_DEBUG
static volatile bool gForce = false;
#endif

// Chỉ gọi khi đang giữ gMux; cả đọc và ghi gLastLevel dùng cùng khóa.
static PumpIn makeIn(const Shared& s, const Config& c) {
  if (s.riv.valid) {
    gLastLevel = s.riv.level_mm;
  }

  PumpIn in;
  in.riv_err = s.riv_err;
  in.riv_lvl = s.riv_lvl;
  in.level_mm = gLastLevel;
  in.gpio_res_cut = (digitalRead(pin::IL_RES_FULL) == LOW); // kiểm ngay khi nhận lệnh và mỗi vòng bơm
  in.res_err = s.res_err;
  in.res_valid = s.res.valid;
  in.res_sensor_full = !s.res_err && s.res.valid && s.res.level_mm >= c.R_full;
  in.pump_off = c.pump_off;
  return in;
}

// P2 gọi khi nhận cmd pump_drain. HÀM NÀY TỰ ACK.
void pumpOnCommand(const char* id, const char* val, int ttl_s) {
  Shared s = sharedCopy();
  Config c = cfgGet();
#ifdef FW_DEBUG
  if (val && !strcmp(val, "force")) { gForce = true; ackPost(id, true, ""); return; }   // chỉ build debug
  if (val && !strcmp(val, "off")) gForce = false;
#endif
  if (!val || (strcmp(val, "on") && strcmp(val, "off"))) {
    ackPost(id, false, "invalid_cmd");
    eventPost("PUMP_BLOCKED", "invalid_cmd", "");
    return;
  }
  const char* why;
  portENTER_CRITICAL(&gMux);
  PumpIn in = makeIn(s, c);
  why = gLogic.request(in, !strcmp(val, "on"), ttl_s, millis());
  portEXIT_CRITICAL(&gMux);
  if (why[0]) { ackPost(id, false, why); eventPost("PUMP_BLOCKED", why, ""); }
  else ackPost(id, true, "");
}

void pumpTask(void*) {
  ledcSetup(CH_DRAIN, 1000, 8);                       // core 3.x: ledcAttach(pin, 1000, 8)
  ledcAttachPin(pin::PUMP_DRAIN, CH_DRAIN);
  ledcWrite(CH_DRAIN, 0);                             // khởi động luôn ở trạng thái tắt
  TickType_t tick = xTaskGetTickCount();
  uint32_t lastSw = 0;
  for (;;) {
    Shared s = sharedCopy();
    Config c = cfgGet();

    const char* blocked; bool on, rea, cmd; uint32_t sw;
    portENTER_CRITICAL(&gMux);
    PumpIn in = makeIn(s, c);
    blocked = gLogic.step(in, millis());
    on = gLogic.on; rea = gLogic.reactive; cmd = gLogic.cmd; sw = gLogic.switches;
    portEXIT_CRITICAL(&gMux);

    if (blocked) eventPost("PUMP_BLOCKED", blocked, "");   // 1 lần mỗi đợt bị chặn

    bool out = on;
#ifdef FW_DEBUG
    if (gForce) out = true;                           // bỏ qua mọi chặn để A kiểm interlock
#endif
    ledcWrite(CH_DRAIN, out ? 255 : 0);               // bơm xả luôn 100% khi bật
    { Lock l; gShared.pump_on = out; gShared.pump_reactive = rea; gShared.pump_cmd_active = cmd; }
    if (sw != lastSw) { Serial.printf("[pump] doi trang thai, tong so lan=%lu\n", (unsigned long)sw); lastSw = sw; }
    vTaskDelayUntil(&tick, pdMS_TO_TICKS(100));
  }
}
#include "levelfsm.h"
static LevelFsm fRiv, fRes;
static bool gCritical = false, gLvlInit = false;

void levelUpdate() {                                  // task sensor (P1) gọi mỗi giây, ngoài Lock
  if (!gLvlInit) {
    pinMode(pin::IL_RES_FULL, INPUT);                 // INPUT thường, KHÔNG pull-up
    pinMode(pin::IL_RIV_OVER, INPUT);
    gLvlInit = true;
  }
  Config c = cfgGet();
  fRiv.setThresholds(c.L1, c.L2, c.L3, c.hyst);
  fRes.setThresholds(c.R_watch, c.R_high, c.R_full, c.hyst);

  Shared s = sharedCopy();
  FsmEvent er = fRiv.step(s.riv.valid, s.riv.level_mm);
  FsmEvent ee = fRes.step(s.res.valid, s.res.level_mm);
  bool resCut = (digitalRead(pin::IL_RES_FULL) == LOW);   // LOW = phao cắt hoặc đứt dây
  bool rivCut = (digitalRead(pin::IL_RIV_OVER) == LOW);

  int rs = fRiv.state(), es = fRes.state();
  bool resConfirmedFull = !fRes.error() && s.res.valid && s.res.level_mm >= c.R_full;   // chỉ siêu âm hợp lệ
  bool crit = (rs == 2 || rs == 3) && (es == 2 || es == 3);
  {
    Lock l;
    gShared.riv_lvl = fRiv.level();  gShared.riv_err = fRiv.error();
    gShared.res_lvl = fRes.level();                   // v6: chỉ theo siêu âm; GPIO32 LOW KHÔNG tự tạo R_FULL
    gShared.res_err = fRes.error();
    gShared.res_float_cut = resCut;                   // GPIO32 == LOW (phao cắt hoặc đứt dây)
    gShared.reservoir_full = resConfirmedFull;        // siêu âm hợp lệ và >= R_full, độc lập GPIO32
    gShared.river_overflow = rivCut;
  }
  if (er.changed) eventPostState("river", LevelFsm::name(false, er.from), LevelFsm::name(false, er.to), s.riv.level_mm);
  if (ee.changed) eventPostState("reservoir", LevelFsm::name(true, ee.from), LevelFsm::name(true, ee.to), s.res.level_mm);
  if (crit && !gCritical) eventPost("CAPACITY_CRITICAL", "", "");
  gCritical = crit;
}
