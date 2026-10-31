#pragma once
#include <Arduino.h>
#include "api.h"

class UltrasonicSensor {
 public:
  UltrasonicSensor(int trig, int echo) : trig_(trig), echo_(echo) {}
  void begin() { pinMode(trig_, OUTPUT); pinMode(echo_, INPUT); digitalWrite(trig_, LOW); }
  // 1 lần bắn. Trả khoảng cách mm, hoặc -1 nếu timeout.
  float shoot() {
    digitalWrite(trig_, LOW);  delayMicroseconds(2);
    digitalWrite(trig_, HIGH); delayMicroseconds(10);
    digitalWrite(trig_, LOW);
    unsigned long us = pulseIn(echo_, HIGH, 30000UL);   // LUÔN có timeout, không thì treo task
    if (us == 0) return -1;
    return us * 0.343f / 2.0f;    // 0,343 mm/µs; chia 2 vì âm đi và về
  }
 private:
  int trig_, echo_;
};

// Sắp xếp chèn rồi lấy số giữa. n nhỏ (<= 5) nên chạy nhanh.
inline float median(float* v, int n) {
  for (int i = 1; i < n; i++) {
    float k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return (n % 2) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

// Đo cả hai cảm biến: mỗi cảm biến 5 mẫu, bắn xen kẽ. Mất khoảng 0,6 đến 0,9 giây.
// Đừng tăng số mẫu hay khoảng nghỉ nếu chưa tính lại thời gian vòng (phải < 1 s).
inline void measureBoth(UltrasonicSensor& riv, UltrasonicSensor& res,
                        const SensorCal& cr, const SensorCal& cs, Meas& mr, Meas& ms) {
  float r[5], s[5]; int nr = 0, ns = 0;
  for (int i = 0; i < 5; i++) {
    float d = riv.shoot();
    if (d >= cr.min_mm && d <= cr.max_mm) r[nr++] = d;
    vTaskDelay(pdMS_TO_TICKS(60));
    d = res.shoot();
    if (d >= cs.min_mm && d <= cs.max_mm) s[ns++] = d;
    vTaskDelay(pdMS_TO_TICKS(60));
  }
  mr.valid = nr >= 3;
  if (mr.valid) { mr.raw_mm = median(r, nr); mr.level_mm = cr.a * mr.raw_mm + cr.b; }
  ms.valid = ns >= 3;
  if (ms.valid) { ms.raw_mm = median(s, ns); ms.level_mm = cs.a * ms.raw_mm + cs.b; }
}
