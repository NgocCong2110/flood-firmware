#pragma once
#include <math.h>

// Các tham số của một kịch bản bơm triều
struct ScenarioParams {
  float period_s;   // chu kỳ sóng (giây)
  float P0;         // mức giữa (%)
  float P1;         // biên độ dao động (%)
  bool  has_surge;  // có đột biến hay không
  float s_t;        // đột biến bắt đầu ở giây thứ mấy
  float s_amp;      // đột biến cộng thêm bao nhiêu %
  float s_w;        // đột biến kéo dài bao nhiêu giây
};

// Công suất cuối cùng (%) tại giây t: sóng sin + đột biến, rồi kẹp ở pwm_max
inline float scenarioPwm(const ScenarioParams& p, float t, float pwm_max) {
  float f = p.P0 + p.P1 * sinf(2.0f * 3.14159265f * t / p.period_s);
  if (f < 0) f = 0;
  if (f > 100) f = 100;
  if (p.has_surge && t >= p.s_t && t <= p.s_t + p.s_w) f += p.s_amp;
  if (f > pwm_max) f = pwm_max;
  if (f > 100) f = 100;
  if (f < 0) f = 0;
  return f;
}

// Kiểm tra tham số hợp lệ (false = từ chối, trả invalid_cmd)
inline bool scenarioValid(const ScenarioParams& p, float pwm_min) {
  if (!isfinite(p.period_s) || !isfinite(p.P0) || !isfinite(p.P1) ||
      !isfinite(p.s_t) || !isfinite(p.s_amp) || !isfinite(p.s_w)) return false;
  if (!(p.period_s >= 10)) return false;               // 10 s: mức tạm, chưa chốt
  if (p.P0 < 0 || p.P0 > 100 || p.P1 < 0 || p.P1 > 100) return false;
  if (p.P0 - p.P1 < pwm_min) return false;             // đáy sóng phải >= pwm_min
  if (p.has_surge && (p.s_t < 0 || p.s_amp < 0 || p.s_w <= 0)) return false;
  return true;
}