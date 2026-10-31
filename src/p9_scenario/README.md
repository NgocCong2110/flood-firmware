# P9 - Bơm triều (kịch bản), camera, kiểm thử lỗi

**Thư mục của P9:** `src/p9_scenario/`, `firmware-cam/`
**File phải tạo:** `scenario.cpp`, `../../../firmware-cam/firmware-cam.ino`
**Nghiệm thu phụ trách:** #7 (báo giả), #9 (lỗi cảm biến), #21 (camera)

## Việc cụ thể theo ngày
**N1 (M0)**
- Nạp FW2 (ESP32-CAM AI Thinker); mở `/capture` (cổng 80) và `/stream` (cổng 81) trên trình duyệt.
- Xong khi: xem được ảnh.

**N2 (M1)**
- IP cố định cho FW2, gửi IP cho P8 và nhóm C.
- Viết `scenario.cpp`: chạy kịch bản bơm triều theo công thức, có surge (`s_t`, `s_amp`, `s_w`).
- Xong khi: nạp lên chạy ra PWM đúng công thức.

**N3 (M2)**
- Test lỗi cảm biến (rút dây, che) và test báo giả; viết cách tái hiện lỗi vào `docs/`.
- Xong khi: biết cách tái hiện lỗi mỗi lần.

**N4 (M3)**
- Kẹp `pwm_max` (PWM thực tế ghi vào `pump_tide_pct`).
- Nối FW2 vào proxy của nhóm C.
- Xong khi: xem camera qua proxy của C.

**N5 (M4):** chạy #7 #9 #21, điền `docs/nghiem-thu/P9.md`.

## Hàm P9 PHẢI cung cấp
`scenarioStart(const ScenarioReq&, const char** reasonOut)` (reason: "" | "clamped" | "invalid_cmd"), `scenarioStop()`, `scenarioTask(void*)`

## Hàm P9 được gọi
`eventPost()`, `ackPost()` (P2); `cfgGet()` (P1)

## Ghi nhớ
- `run_id`: dùng giá trị trong lệnh nếu có, không thì tự sinh `r<boot>-<số thứ tự>` (D6); rỗng khi không chạy kịch bản.
- FW2 không qua MQTT; DevKit không biết cam còn sống hay không.
- GPIO19 (phao sông) vật lý cắt EN của bơm B.
- `source` luôn `"real"`, không có `sim` trong bản nghiệm thu.

## Cấm
Không sửa file P1/P2/P7. Kịch bản không được vượt `pwm_max`.
