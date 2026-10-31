#include <Arduino.h>
#include <esp_task_wdt.h>
#include <math.h>
#include "api.h"
#include "pins.h"
#include "ultrasonic.h"

Shared gShared;
SemaphoreHandle_t gMtx;

// Kiểm nhảy: chỉ so với mẫu trước nếu mẫu trước hợp lệ.
struct JumpGuard {
  bool prevOk = false; float prevLevel = 0;
  void check(Meas& m, float jump_mm) {
    if (m.valid && prevOk && fabsf(m.level_mm - prevLevel) > jump_mm) m.valid = false;
    prevOk = m.valid;
    if (m.valid) prevLevel = m.level_mm;
  }
};

void sensorTask(void*) {
  UltrasonicSensor riv(pin::RIV_TRIG, pin::RIV_ECHO), res(pin::RES_TRIG, pin::RES_ECHO);
  riv.begin(); res.begin();
  JumpGuard jr, js;
  esp_task_wdt_add(NULL);                     // task này nằm trong danh sách được watchdog canh
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    Config c = cfgGet();
    SensorCal cr = calGet(false), cs = calGet(true);
    Meas mr{}, ms{};      // khởi tạo 0 mỗi vòng: không để lại giá trị rác khi mẫu hỏng
    measureBoth(riv, res, cr, cs, mr, ms);
    jr.check(mr, c.jump_mm);
    js.check(ms, c.jump_mm);

    { Lock l; gShared.riv = mr; gShared.res = ms; }

    // CSV cho hiệu chuẩn, dán thẳng vào Excel được
    Serial.printf("CSV,%.1f,%.1f,%d,%.1f,%.1f,%d\n", mr.raw_mm, mr.level_mm, mr.valid,
                  ms.raw_mm, ms.level_mm, ms.valid);

    levelUpdate();      // P7 (gọi NGOÀI Lock)
    sampleCommit();     // P2

    esp_task_wdt_reset();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));   // đủ 1 giây giữa hai lần bắt đầu đo
  }
}

void setup() {
  Serial.begin(115200);

  // 1) Tắt cả hai bơm TRƯỚC TIÊN: kéo chân PWM xuống LOW.
  //    Cấu hình LEDC/PWM thuộc P7 (GPIO25, pumpTask) và P9 (GPIO33, scenarioTask); P1 không cấu hình PWM.
  pinMode(pin::PUMP_DRAIN, OUTPUT); digitalWrite(pin::PUMP_DRAIN, LOW);
  pinMode(pin::PUMP_TIDE,  OUTPUT); digitalWrite(pin::PUMP_TIDE,  LOW);

  // 2) Chân phao: INPUT thường, KHÔNG pull-up (để đứt dây đọc ra LOW)
  pinMode(pin::IL_RES_FULL, INPUT);
  pinMode(pin::IL_RIV_OVER, INPUT);

  // 3) Khoá chung và NVS (gMtx phải có trước settingsLoad)
  gMtx = xSemaphoreCreateMutex();
  settingsLoad();                       // đọc boot, Config, SensorCal
  netInit();                            // P2: tạo hàng đợi. PHẢI gọi trước khi tạo task

  // 4) Watchdog 30 giây (core 2.0.x). Core 3.x dùng esp_task_wdt_reconfigure.
  esp_task_wdt_init(30, true);

  // 5) Tạo task. Số cuối: core 0 hoặc 1. Stack là ƯỚC LƯỢNG, chỉnh theo thực tế (xem "Stack canary" trên Serial).
  xTaskCreatePinnedToCore(sensorTask,   "sensor",   6144, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(pumpTask,     "pump",     4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(scenarioTask, "scenario", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(uiTask,       "ui",       3072, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(netTask,      "net",      8192, NULL, 2, NULL, 0);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }   // không làm gì, các task lo hết
