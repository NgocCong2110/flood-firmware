#pragma once
// Giao diện chung giữa P1, P2, P7, P9. P1 sở hữu file này: ai cần đổi thì báo P1 và sửa hợp đồng mục 7.6.
// Chưa biên dịch cho ESP32 (chỉ kiểm cú pháp với bản giả lập). Tên trường ngoài hợp đồng là GIẢ ĐỊNH, đối chiếu khi ghép code.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// ---------- Kiểu dữ liệu ----------
struct Meas { bool valid; float raw_mm; float level_mm; };   // một lần đo của một cảm biến

// Hệ số hiệu chuẩn. SỐ MẶC ĐỊNH NÀY LÀ GIẢ ĐỊNH, phải thay bằng số đo thật (min/max đo Ngày 1, a/b hiệu chuẩn Ngày 3).
struct SensorCal { float a = -1.0f, b = 500.0f, min_mm = 250.0f, max_mm = 600.0f; };

// Cấu hình (topic `config`, hợp đồng mục 7). Toàn bộ 4 byte/trường để memcmp và lưu NVS không dính padding.
struct Config {
  float   L1 = 80, L2 = 120, L3 = 160, hyst = 10;
  float   pump_off = 110, H_max = 200;
  float   R_watch = 120, R_high = 160, R_full = 180;
  float   jump_mm = 50;
  int32_t pwm_min = 20, pwm_max = 70, cmd_ttl_s = 30;   // pwm_min = 20 là TẠM, chưa đo
  float   r2_min = 0.85f;
  int32_t pre_alert_s = 60, pre_drain_s = 30, fc_stale_s = 10;
};

// Lệnh kịch bản đã được P2 kiểm cấu trúc (hợp đồng 7.4). P9 kiểm giá trị.
struct ScenarioReq {
  bool  start;            // true = start, false = stop
  char  run_id[12];       // <= 11 ký tự
  float period_s, P0, P1;
  bool  has_surge;
  float s_t, s_amp, s_w;
};

// Trạng thái dùng chung. Đọc/ghi phải qua Lock hoặc sharedCopy().
struct Shared {
  Meas riv{}, res{};
  int  riv_lvl = 0, res_lvl = 0;                 // 0..3 (NORMAL..DANGER, R_OK..R_FULL)
  bool riv_err = false, res_err = false;         // SENSOR_ERROR / R_SENSOR_ERROR
  bool res_float_cut = false;                    // GPIO32 == LOW
  bool reservoir_full = false;                   // siêu âm hợp lệ và >= R_full (độc lập GPIO32)
  bool river_overflow = false;                   // GPIO19 == LOW
  bool pump_on = false, pump_reactive = false, pump_cmd_active = false;
  int  pump_tide_pct = 0;                        // công suất bơm triều thực tế (sau kẹp)
  char run_id[12] = "";                          // lượt chạy kịch bản; rỗng nếu không chạy
  bool mqtt_ok = false;                          // P2 cập nhật
  uint32_t boot = 0;                             // số lần khởi động (P1 ghi sau settingsLoad); P9 dùng khi tự sinh run_id
};

extern Shared gShared;
extern SemaphoreHandle_t gMtx;
extern uint32_t boot_count;
extern volatile uint32_t gNetBeat;

// Khoá chung. KHÔNG lồng (mutex thường): đừng gọi sharedCopy() hay eventPost() khi đang giữ Lock.
struct Lock {
  Lock()  { xSemaphoreTake(gMtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gMtx); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};
inline Shared sharedCopy() { Lock l; return gShared; }

// ---------- P1: cấu hình, NVS, task đo ----------
void      settingsLoad();                       // đọc NVS, tăng boot, ghi gShared.boot
Config    cfgGet();
void      cfgSet(const Config& c);              // chỉ ghi NVS khi có thay đổi thật
SensorCal calGet(bool reservoir);               // false = sông, true = hồ
bool      calibrateApply(const char* sensor, float a, float b, const char** why);   // sensor bắt buộc: "river"/"reservoir"
void      sensorTask(void*);

// ---------- P2: mạng, MQTT, ACK ----------
void netInit();                                 // tạo hàng đợi; gọi 1 lần trong setup, TRƯỚC khi tạo task
void netTask(void*);
void sampleCommit();                            // P1 gọi mỗi giây
void eventPost(const char* type, const char* a, const char* b);                          // KHÔNG gọi khi đang giữ Lock
void eventPostState(const char* scope, const char* from, const char* to, float level_mm); // sự kiện đổi mức
void ackPost(const char* id, bool ok, const char* reason);                               // ACK luôn mang đúng id của lệnh

// ---------- P7: cảnh báo, bơm xả, LED, còi ----------
void levelUpdate();                             // P1 gọi mỗi giây, NGOÀI Lock
void pumpTask(void*);
void uiTask(void*);
void pumpOnCommand(const char* id, const char* val, int ttl_s);   // P2 gọi; hàm tự ACK

// ---------- P9: kịch bản bơm triều ----------
void scenarioTask(void*);
bool scenarioStart(const ScenarioReq& r, const char** why);       // P2 gọi, rồi P2 tự ACK
bool scenarioStop(const char* run_id, const char** why);
