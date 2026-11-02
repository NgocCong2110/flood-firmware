#include "ultrasonic.h"

void UltrasonicSensor::begin() {
  pinMode(trig_, OUTPUT);
  pinMode(echo_, INPUT);
  digitalWrite(trig_, LOW);
}

float UltrasonicSensor::shoot() {
  digitalWrite(trig_, LOW);
  delayMicroseconds(2);
  digitalWrite(trig_, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig_, LOW);

  const unsigned long us = pulseIn(echo_, HIGH, 30000UL);
  if (us == 0) return -1.0f;
  return us * 0.343f / 2.0f;
}

float median(float* v, int n) {
  for (int i = 1; i < n; ++i) {
    const float value = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > value) {
      v[j + 1] = v[j];
      --j;
    }
    v[j + 1] = value;
  }
  return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

void measureBoth(UltrasonicSensor& riv, UltrasonicSensor& res,
                 const SensorCal& cr, const SensorCal& cs, Meas& mr, Meas& ms) {
  float r[5], s[5];
  int nr = 0, ns = 0;
  for (int i = 0; i < 5; ++i) {
    float distance = riv.shoot();
    if (distance >= 0 && distance >= cr.min_mm && distance <= cr.max_mm) r[nr++] = distance;
    vTaskDelay(pdMS_TO_TICKS(60));

    distance = res.shoot();
    if (distance >= 0 && distance >= cs.min_mm && distance <= cs.max_mm) s[ns++] = distance;
    vTaskDelay(pdMS_TO_TICKS(60));
  }

  mr = {};
  ms = {};
  mr.valid = nr >= 3;
  if (mr.valid) {
    mr.raw_mm = median(r, nr);
    mr.level_mm = cr.a * mr.raw_mm + cr.b;
  }
  ms.valid = ns >= 3;
  if (ms.valid) {
    ms.raw_mm = median(s, ns);
    ms.level_mm = cs.a * ms.raw_mm + cs.b;
  }
}
