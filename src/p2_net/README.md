# P2 - Mạng, MQTT, đệm dữ liệu

**Thư mục của P2:** `src/p2_net/` · **Nghiệm thu:** #4, #8, #10 (cùng nhóm C), #23 (lệnh MQTT, cùng P9/C)
**Theo:** hợp đồng chung v6.1 DRAFT mục 7; `P2-dat.md` v8.1.

## File

| File | Nội dung | Trạng thái |
|---|---|---|
| `ringbuf.h` | Vòng đệm 900 bản + đánh dấu đã gửi / gửi bù (`TxRing`) | Test PC đạt (`test/p2_net`) |
| `netcmd.h` | Kiểm cấu trúc `cmd` / `scenario` / `config` (chỉ ArduinoJson) | Test PC đạt (`test/p2_net`) |
| `net.cpp` | Wi-Fi, NTP, MQTT, telemetry, status/LWT, ACK, `netTask` | Chỉ kiểm biên dịch với header giả trên PC. **Chưa biên dịch ESP32, chưa nạp board** |

## Trước khi biên dịch
1. **Xoá 7 dòng P2 trong `src/common/stubs.cpp`** (P1 sở hữu, báo P1): `sampleCommit`, `netTask`, `eventPost`, `ackPost`, `netInit`, `eventPostState`, `gNetBeat`. Không xoá thì lỗi trùng định nghĩa.
2. Tạo `src/common/secrets.h` (không commit):
   ```cpp
   #define WIFI_SSID "ten-wifi"
   #define WIFI_PASS "mat-khau"
   #define MQTT_HOST "192.168.1.10"   // IP laptop chạy Mosquitto
   // tuỳ chọn: #define MQTT_PORT 1883, #define DEVICE_ID "node01", #define NTP_HOST "192.168.1.10"
   ```
3. `pio run` rồi `pio run -t upload`, xem `pio device monitor`.

## Test trên PC (không cần board)
```
g++ -std=c++17 -Wall -Isrc/p2_net -I.pio/libdeps/esp32dev/ArduinoJson/src test/p2_net/test_p2.cpp -o t && ./t
```
(`.pio/libdeps/...` có sau lần `pio run` đầu; hoặc clone ArduinoJson 7 và trỏ `-I` vào `src` của nó.)

Công cụ MQTT (ESP32 giả, chạy ca thử lệnh, kiểm #8): `tools/p2_net/README.md`.

## Quyết định P2 đã áp dụng (ghi vào `docs/quyet-dinh.md`)
- **ACK không chờ NTP** (hợp đồng 7.5: `ack` không có `ts`; quyết định #9 chỉ chặn telemetry/event). ACK có hàng đợi riêng, gửi ngay khi MQTT nối. Thay cho cách tạm "ACK chờ có giờ" ở `P2-dat.md` §13. **Cần trưởng nhóm B xác nhận.**
- `mode` trong telemetry lấy từ lệnh `{"mode":...}` gần nhất (khác D5).
- `wifi_ok`, `rssi`, `time_ok` giữ trong `net.cpp` (không thêm vào `Shared`). P2 chỉ ghi `gShared.mqtt_ok`.
- Stop có `run_id` khác lượt đang chạy: P2 vẫn chuyển cho P9 (P9 dừng, ACK ok) theo v6.1. P2 không so `run_id`.
- `config`: chỉ nhận khi `L1<L2<L3≤H_max`, `R_watch<R_high<R_full`, `0<pump_off<L3`, `0≤pwm_min≤pwm_max≤100`, `1≤cmd_ttl_s≤300`. Trường sai kiểu bị lờ đi.

## Ghi chú cho người khác
- **P9:** gọi `eventPost("RUN_STOP", ...)` **trước** khi xoá `gShared.run_id` (event chép `run_id` lúc gọi).
- **P7, P9:** không gọi `eventPost`/`eventPostState` khi đang giữ `Lock` hoặc trong `portENTER_CRITICAL` (hàm lấy `Lock`). `ackPost` không lấy `Lock`.
- **Nhóm C:** sẽ có bản telemetry trùng sau khi mất liên kết (gửi lại 40 s gần nhất); bỏ trùng theo `(device_id, boot, seq)`.

## Cấm / dễ sai
- Không khởi động lại board vì mất mạng. Không `delay()` dài trong `netTask`.
- Wi-Fi không ra Internet thì không có NTP, firmware **không gửi telemetry**: đặt `NTP_HOST` về máy trong LAN.
