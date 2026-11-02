#pragma once
#include <Arduino.h>
#include "api.h"

class UltrasonicSensor {
 public:
  UltrasonicSensor(int trig, int echo) : trig_(trig), echo_(echo) {}
  void begin();
  // Trả khoảng cách mm, hoặc -1 nếu timeout.
  float shoot();

 private:
  int trig_, echo_;
};

// Sắp xếp mảng tại chỗ; n phải lớn hơn 0.
float median(float* v, int n);

// Mỗi cảm biến đo 5 mẫu xen kẽ; cần ít nhất 3 mẫu hợp lệ.
void measureBoth(UltrasonicSensor& riv, UltrasonicSensor& res,
                 const SensorCal& cr, const SensorCal& cs, Meas& mr, Meas& ms);
