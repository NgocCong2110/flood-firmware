# P7 - Cảnh báo, bơm xả, LED, còi

**Thư mục của P7:** `src/p7_alert/`, `test/p7_logic/`
**File phải tạo:** `levelfsm.h/.cpp`, `pump.cpp`, `ui.cpp`, `../../test/p7_logic/test_logic.cpp`
**Nghiệm thu phụ trách:** #5 (mức sông), #6 (mức hồ), #13 (bơm xả REACTIVE), #18 (kịch bản có surge)
**Ghi chú:** P7 nhiều việc nhất. Nếu trễ, P9 sang giúp LED/còi.

## Việc cụ thể theo ngày
**N1 (M0)**
- Viết `LevelFsm` (4 mức NORMAL, WATCH, WARNING, DANGER; hồ R_OK..R_FULL; hysteresis) và test trên PC bằng g++.
- Xong khi: test PC qua hết.

**N2 (M1)**
- Gắn `LevelFsm` vào board; viết `levelUpdate()` (tính mức, đọc GPIO32/GPIO19 - D4); LED cơ bản.
- Xong khi: đổ nước thì LED đổi theo.

**N3 (M2)**
- Event (đổi mức, `PUMP_BLOCKED`...), còi, `CAPACITY_CRITICAL`.
- Xong khi: đổ nước qua L1, L2, L3 đúng mức, đúng event.

**N4 (M3)**
- Bơm xả REACTIVE (`pumpTask`), chặn an toàn, TTL lệnh (`pumpOnCommand`).
- Bản `env:debug` (FW_DEBUG=1) để ép bơm kiểm interlock.
- Xong khi: lệnh bật khi hồ đầy hoặc `level < 110` bị từ chối đúng `reason`; rút dây cảm biến thì `SENSOR_ERROR` trong 10 giây và bơm xả tắt.

**N5 (M4):** chạy #5 #6 #13 #18, điền `docs/nghiem-thu/P7.md`.

## Hàm P7 PHẢI cung cấp
`levelUpdate()`, `pumpOnCommand(id, val, ttl_s)`, `pumpTask(void*)`, `uiTask(void*)`

## Hàm P7 được gọi
`eventPost()`, `ackPost()` (P2); `cfgGet()` (P1)

## Quy tắc an toàn
- GPIO32 (hồ đầy) và GPIO19 (sông tràn): `INPUT` thường, KHÔNG pull-up; LOW = đã cắt hoặc đứt dây.
- Bơm xả chỉ bị chặn bởi GPIO32 (D9), chờ trưởng nhóm xác nhận Q3.
- LA38 (nút dừng khẩn cấp) bấm thì ESP32 vẫn sống, firmware vẫn báo bình thường, không coi là lỗi.
- `env:debug` KHÔNG nạp khi đóng băng M4.

## Cấm
Không sửa `ultrasonic.*`, `net.cpp`, `scenario.cpp`. Không delay/publish khi giữ Lock.
