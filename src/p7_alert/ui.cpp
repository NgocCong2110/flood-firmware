// OWNER: P7
// LED xanh/vàng/đỏ và còi.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.
#include <Arduino.h>
#include "api.h"
#include "pins.h"

#ifndef BUZZER_MUTE
#define BUZZER_MUTE 0           // đặt 1 khi test ở nơi yên tĩnh. Giả định buzzer chủ động (active)
#endif

static int severity(const Shared& s) {                // D11
  int a = s.riv_err ? 4 : s.riv_lvl;
  int b = s.res_err ? 4 : s.res_lvl;
  return a > b ? a : b;
}

void uiTask(void*) {
  pinMode(pin::LED_G, OUTPUT); pinMode(pin::LED_Y, OUTPUT);
  pinMode(pin::LED_R, OUTPUT); pinMode(pin::BUZZER, OUTPUT);
  TickType_t tk = xTaskGetTickCount();
  uint32_t t = 0;                                     // 1 tick = 100 ms
  for (;;) {
    Shared s = sharedCopy();
    int sev = severity(s);
    bool g = false, y = false, r = false, bz = false;
    switch (sev) {
      case 0: g = true; break;                                      // xanh
      case 1: y = (t % 20) < 10; break;                             // vàng nháy chậm
      case 2: y = true; bz = (t % 20) < 2; break;                   // vàng sáng, bíp chậm
      case 3: r = true; bz = (t % 4) < 2; break;                    // đỏ sáng, bíp nhanh
      default: r = (t % 6) < 3; bz = (t % 30) < 10; break;          // đỏ nháy, bíp dài ngắt quãng
    }
    if (!s.mqtt_ok) {                                               // mất mạng: nháy xanh ngắn
      if (sev == 0) g = (t % 30) != 0; else g = (t % 30) == 0;
    }
    digitalWrite(pin::LED_G, g); digitalWrite(pin::LED_Y, y); digitalWrite(pin::LED_R, r);
    digitalWrite(pin::BUZZER, (!BUZZER_MUTE && bz) ? HIGH : LOW);
    t++;
    vTaskDelayUntil(&tk, pdMS_TO_TICKS(100));
  }
}
