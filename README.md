# flood-firmware (Nhóm B)

Firmware cho mô hình giám sát mực nước sông + hồ điều tiết.
- **FW1**: ESP32 DevKit V1 - đo, cảnh báo, bơm, LED, còi, đệm dữ liệu, MQTT.
- **FW2**: ESP32-CAM - chỉ camera (HTTP `/capture` cổng 80, `/stream` cổng 81).

## Cấu trúc & phân chia file

Build: `pio run` (bản thường) hoặc `pio run -e debug` (bản debug, chỉ để nhóm A kiểm phao, không nạp lúc nghiệm thu).
Nạp: `pio run -t upload`. Xem Serial: `pio device monitor`.

Mỗi người một thư mục (CHỈ SỬA THƯ MỤC CỦA MÌNH). Mỗi thư mục có `README.md` riêng: việc theo ngày, hàm phải cung cấp, nghiệm thu, điều cấm.

| Người | Thư mục | File |
|---|---|---|
| **P1** | `src/common/`, `src/p1_sensor/`, `src/main.cpp`, `platformio.ini`, `tools/p1_calib/` | `api.h`, `pins.h`, `stubs.cpp`, `secrets.example.h`, `settings.cpp`, `ultrasonic.h`, `calib.py` (ai cần đổi `api.h` thì báo P1) |
| **P2** | `src/p2_net/` | `net.cpp`, `ringbuf.h` |
| **P7** | `src/p7_alert/`, `test/p7_logic/` | `levelfsm.*`, `pump.cpp`, `ui.cpp`, test chạy trên PC (g++) |
| **P9** | `src/p9_scenario/`, `firmware-cam/` | `scenario.cpp`, ESP32-CAM |

Khi thêm file thật của mình, **xoá hàm tương ứng khỏi `src/common/stubs.cpp`**, nếu không sẽ lỗi trùng định nghĩa.
Chưa biên dịch cho ESP32. Số mặc định trong `api.h` (`a`, `b`, `min_mm`, `max_mm`, `pwm_min`) là giả định, phải thay bằng số đo thật.

Bảng nghiệm thu: `docs/nghiem-thu/`.

## Quy tắc chung
1. Tên topic, trường JSON, GPIO, ngưỡng: đúng từng ký tự theo hợp đồng (`00-hop-dong-chung.md` v5). Chỗ mơ hồ ghi vào `docs/cau-hoi-mo.md`, KHÔNG tự sửa hợp đồng.
2. Chỉ gọi hàm người khác qua `src/common/api.h`.
3. Mỗi người một nhánh: `p1-sensor`, `p2-net`, `p7-fsm`, `p9-cam`. Commit nhỏ, merge vào `main` khi test xong.
4. `src/common/secrets.h` KHÔNG commit (dùng `secrets.example.h` làm mẫu).
5. Số đo nghiệm thu chỉ dùng dữ liệu `real`. Chưa đo thì ghi "chưa đo".
6. Code chưa biên dịch/nạp board thì phải nói rõ "chưa test trên board".

## Lịch
- N1 (29/10) M0: board sống, cảm biến đo được
- N2 (30/10) M1: số liệu thật lên máy chủ
- N3 (31/10) M2: cảnh báo và chịu mất mạng
- N4 (1/11) M3: bơm và an toàn
- N5 (2/11) M4: nghiệm thu, 18:00 đóng băng, nạp `env:esp32dev` (không phải `env:debug`)

Tài liệu gốc: `docs/00-doc-truoc-ca-nhom.md` (bỏ file md vào đây).
