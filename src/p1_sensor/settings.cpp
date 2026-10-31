#include <Preferences.h>
#include <cstring>
#include <math.h>
#include "api.h"

uint32_t boot_count = 0;

static Config    cfg;
static SensorCal calRiv, calRes;
static Preferences prefs;
static SemaphoreHandle_t cfgMtx = nullptr;

// Đọc blob từ NVS; nếu chưa có hoặc sai kích thước (do đổi struct) thì giữ mặc định.
template <typename T> static void loadBlob(const char* key, T& out) {
  T tmp;
  if (prefs.getBytesLength(key) == sizeof(T) && prefs.getBytes(key, &tmp, sizeof(T)) == sizeof(T)) out = tmp;
}

void settingsLoad() {                    // gMtx phải được tạo TRƯỚC khi gọi hàm này (xem setup)
  cfgMtx = xSemaphoreCreateMutex();
  prefs.begin("flood", false);
  boot_count = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", boot_count);
  loadBlob("cfg",  cfg);
  loadBlob("calR", calRiv);
  loadBlob("calS", calRes);
  { Lock l; gShared.boot = boot_count; }
}

Config cfgGet() {
  xSemaphoreTake(cfgMtx, portMAX_DELAY); Config c = cfg; xSemaphoreGive(cfgMtx); return c;
}

void cfgSet(const Config& c) {
  xSemaphoreTake(cfgMtx, portMAX_DELAY);
  bool changed = memcmp(&cfg, &c, sizeof(Config)) != 0;
  if (changed) { cfg = c; prefs.putBytes("cfg", &cfg, sizeof(Config)); }
  xSemaphoreGive(cfgMtx);
}

SensorCal calGet(bool reservoir) {
  xSemaphoreTake(cfgMtx, portMAX_DELAY);
  SensorCal c = reservoir ? calRes : calRiv;
  xSemaphoreGive(cfgMtx);
  return c;
}

// sensor: "river" hoặc "reservoir" (đúng chữ thường), BẮT BUỘC, không có mặc định.
// Trả true nếu đã lưu. Trả false thì *why = "invalid_cmd", hệ số cũ giữ nguyên, không ghi NVS.
bool calibrateApply(const char* sensor, float a, float b, const char** why) {
  bool reservoir;
  if (sensor && strcmp(sensor, "river") == 0) reservoir = false;
  else if (sensor && strcmp(sensor, "reservoir") == 0) reservoir = true;
  else { if (why) *why = "invalid_cmd"; return false; }
  // Chặn số vô lý. Giới hạn này do nhóm tự chọn, không có trong hợp đồng (mục 15 #18).
  if (!isfinite(a) || !isfinite(b) || a == 0 || fabsf(a) > 5 || b < 0 || b > 2000) {
    if (why) *why = "invalid_cmd";
    return false;
  }
  xSemaphoreTake(cfgMtx, portMAX_DELAY);
  SensorCal& c = reservoir ? calRes : calRiv;
  c.a = a; c.b = b;
  prefs.putBytes(reservoir ? "calS" : "calR", &c, sizeof(SensorCal));
  xSemaphoreGive(cfgMtx);
  if (why) *why = "";
  return true;
}
