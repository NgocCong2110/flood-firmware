# P2 - Mạng, MQTT, đệm dữ liệu

**Thư mục của P2:** `src/p2_net/`
**File phải tạo:** `net.cpp`, `ringbuf.h`
**Nghiệm thu phụ trách:** #4 (độ trễ cảnh báo), #8 (mất Wi-Fi 5 phút), #10 (phục hồi) - làm cùng nhóm C

## Việc cụ thể theo ngày
**N1 (M0)**
- Kết nối Wi-Fi + MQTT tới broker của nhóm C, gửi thử 1 tin.
- **Xác nhận hợp đồng v5 với nhóm C** (đặc biệt Q2: `status` là chuỗi trơn hay JSON).
- Xong khi: `mosquitto_sub -h <IP> -t 'flood/node01/#' -v` thấy tin thử.

**N2 (M1)**
- Telemetry 1 bản/giây, `status`, LWT, `boot`/`seq`, NTP.
- `setBufferSize(768)` cho PubSubClient.
- Xong khi: bản tin đủ trường đúng hợp đồng; có bản ghi `real` trong DB của C.

**N3 (M2)**
- `RingBuf` (đệm 900 bản, BIẾN TOÀN CỤC) và replay (gửi bù, cờ `replay`).
- Đo độ trễ (#4, chỉ tính `replay=false`).
- Xong khi: rút Wi-Fi 5 phút rồi nối lại, dữ liệu bù đúng thứ tự, không mất mẫu.

**N4 (M3)**
- Nhận `cmd`, `scenario`, `config` đầy đủ; gửi `ack` (ok hoặc từ chối kèm `reason`).
- Hàng đợi FreeRTOS cho `eventPost`/`ackPost` (an toàn đa task).
- Xong khi: gửi lệnh từ `mosquitto_pub` thì nhận đúng ack.

**N5 (M4):** chạy #4 #8 #10 cùng C, điền `docs/nghiem-thu/P2.md`.

## Hàm P2 PHẢI cung cấp
`sampleCommit()`, `eventPost(type, reason, scope)`, `ackPost(id, ok, reason)`, `netTask(void*)`

## Hàm P2 được gọi
`cfgSet()`, `calibrateApply()` (P1); `pumpOnCommand()` (P7); `scenarioStart()`, `scenarioStop()` (P9)

## Câu hỏi mở của P2 (ghi vào docs/cau-hoi-mo.md, hỏi nhóm C)
Q1 (calibrate áp cảm biến nào), Q2 (định dạng status), Q8 (tên topic/trường của event, ack, scenario, config).

## Cấm / dễ sai
- KHÔNG khởi động lại board khi mất mạng (mất RAM đệm, trượt #8).
- Không `delay(1000)` trong loop (rớt keepalive).
- Không publish trong lúc giữ Lock.
- Mọi tên topic/trường đối chiếu hợp đồng mục 7, chỗ đoán ghi "ĐỐI CHIẾU HỢP ĐỒNG".
