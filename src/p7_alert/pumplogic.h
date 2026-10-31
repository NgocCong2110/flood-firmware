#pragma once
#include <stdint.h>

struct PumpIn
{
  bool riv_err;         // sông SENSOR_ERROR
  int riv_lvl;          // 0..3, 3 = DANGER
  float level_mm;       // mực sông hợp lệ gần nhất
  bool gpio_res_cut;    // GPIO32 == LOW (phần cứng đã ngắt EN)
  bool res_err;         // hồ ở R_SENSOR_ERROR
  bool res_valid;       // mẫu hồ hiện tại hợp lệ (v6.1)
  bool res_sensor_full; // cảm biến hồ HỢP LỆ (không lỗi) và tức thời >= R_full
  float pump_off;
};

// "" nếu không bị chặn; ngược lại đúng tên reason ở hợp đồng mục 7.
// Thứ tự theo hợp đồng 6.1. GPIO32 LOW KHÔNG tự suy ra hồ đầy; reservoir_full chỉ từ số đo siêu âm hợp lệ.
// v6.1 (tạm, chờ xác nhận): "GPIO32 HIGH + cảm biến hồ lỗi" (mẫu không hợp lệ hoặc R_SENSOR_ERROR) → chặn interlock_off.
inline const char *pumpBlockReason(const PumpIn &in)
{
  if (in.riv_err)
    return "sensor_error";
  if (in.gpio_res_cut)
    return in.res_sensor_full ? "reservoir_full" : "interlock_off";
  if (in.res_sensor_full)
    return "reservoir_full";
  if (in.res_err || !in.res_valid)
    return "interlock_off"; // v6.1: hồ lỗi thì không chắc chưa đầy → chặn
  return "";
}

struct PumpLogic
{
  bool on = false, reactive = false, cmd = false;
  uint32_t cmd_until = 0, last_change = 0, switches = 0;
  bool ever = false;
  static const uint32_t MIN_GAP_MS = 5000;

  // Lệnh bật/tắt từ backend. Trả "" nếu nhận, ngược lại lý do từ chối.
  const char *request(const PumpIn &in, bool want, int ttl_s, uint32_t now)
  {
    if (!want)
    {
      cmd = false;
      return "";
    }

    // Lệnh bật chỉ nhận TTL từ 1 đến 300 giây.
    // Giá trị mặc định khi thiếu trường do P2 áp dụng trước khi gọi P7.
    if (ttl_s < 1 || ttl_s > 300)
      return "invalid_cmd";

    const char *r = pumpBlockReason(in);
    if (r[0])
      return r;
    if (in.level_mm < in.pump_off)
      return "below_pump_off";
    cmd = true;
    cmd_until = now + (uint32_t)ttl_s * 1000u;
    return "";
  }

  // Gọi mỗi 100 ms. Trả lý do nếu bơm VỪA bị chặn trong khi đang được yêu cầu, ngược lại nullptr.
  const char *step(const PumpIn &in, uint32_t now)
  {
    const char *r = pumpBlockReason(in);
    if (r[0])
    {
      bool wasRequested = on || reactive || cmd;
      reactive = false;
      cmd = false;
      setOn(false, now);
      return wasRequested ? r : nullptr;
    }
    if (in.level_mm < in.pump_off)
    { // tắt khi < pump_off, độc lập state
      reactive = false;
      cmd = false;
      setOn(false, now);
      return nullptr;
    }
    if (cmd && (int32_t)(now - cmd_until) >= 0)
      cmd = false; // hết TTL → rơi về REACTIVE
    if (in.riv_lvl == 3)
      reactive = true; // sông vào DANGER → bật
    bool want = reactive || cmd;
    if (!want)
      setOn(false, now);
    else if (!on && (!ever || now - last_change >= MIN_GAP_MS))
      setOn(true, now);
    return nullptr;
  }

private:
  void setOn(bool v, uint32_t now)
  {
    if (v != on)
    {
      on = v;
      last_change = now;
      ever = true;
      switches++;
    }
  }
};