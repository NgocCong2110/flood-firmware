# P1 - Đo khoảng cách, hiệu chuẩn, cấu hình, khung chương trình

**Thư mục của P1:** `src/p1_sensor/`, `src/main.cpp`, `src/common/`, `tools/p1_calib/`
**File phải tạo:** `ultrasonic.h/.cpp`, `settings.cpp`, `../main.cpp`, `../../tools/p1_calib/calib.py`
**Nghiệm thu phụ trách:** #1 (MAE sông), #2 (MAE hồ), #3 (độ lặp)

## Việc cụ thể theo ngày
**N1 (M0)**
- Tạo repo, push 4 file chung (`platformio.ini`, `pins.h`, `api.h`, `secrets.example.h`) + `.gitignore`.
- Nạp FW1 trống, in `raw_mm` của 2 cảm biến qua Serial.
- Đo `min_mm`, `max_mm` mỗi cảm biến; thử tiếng dội từ thành hộp.
- Xong khi: Serial in `raw_mm` ổn định cho cả hai cảm biến.

**N2 (M1)**
- Đo 5 mẫu, lấy trung vị; xác định `valid` (>= 3/5 mẫu dùng được, D7); kiểm nhảy `jump_mm`; mẫu đầu luôn chấp nhận (D8).
- Hiệu chuẩn `level_mm = a * raw_mm + b`; lưu/đọc NVS; `boot_count` +1 mỗi lần khởi động.
- Viết `main.cpp`: tạo mutex `gMtx`, tạo các task, watchdog.
- `sensorTask` mỗi 1 giây, gọi nối tiếp: đo -> `levelUpdate()` (P7) -> `sampleCommit()` (P2).
- Xong khi: mực nước đo thử đúng so với thước.

**N3 (M2)**
- Đo 8 mốc sông, 6 mốc hồ, mỗi mốc 3 lần; lưu CSV gốc vào `tools/p1_calib/data/`; tính `a`, `b`.
- Chạy nghiệm thu #1 #2 #3, điền `docs/nghiem-thu/P1.md`.

**N4 (M3)**
- Phối hợp A đo `pwm_min`, `pwm_max` (đo ở điện áp nhánh bơm thật ~8,5V); làm bảng lưu lượng theo PWM.
- Hỗ trợ đo lại khi cần.

**N5 (M4):** điền bảng nghiệm thu bằng số đo thật; 18:00 đóng băng.

## Hàm P1 PHẢI cung cấp (đúng tên trong api.h)
`settingsLoad()`, `cfgGet()`, `cfgSet()`, `calGet(bool reservoir)`, `calibrateApply(bool reservoir, a, b)`, `sensorTask(void*)`

## Hàm P1 được gọi
`levelUpdate()` (P7), `sampleCommit()` (P2)

## Cấm / dễ sai
- Echo JSN-SR04T 5V phải qua chia áp 1k/2k trước khi cấp nguồn.
- `pulseIn` luôn có timeout 30000 us.
- Hai cảm biến bắn xen kẽ, không bắn cùng lúc.
- GPIO32/19 là INPUT thường, không INPUT_PULLUP (nhắc P7).
- Mảng lớn là biến toàn cục.
- Không tự sửa file của P2/P7/P9.
