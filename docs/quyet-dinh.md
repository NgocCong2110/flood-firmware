# Quyết định của nhóm (không có trong hợp đồng gốc)

| # | Quyết định | Lý do |
|---|---|---|
| D1 | Bỏ task `state`; task `sensor` gọi nối tiếp đo -> levelUpdate() -> sampleCommit() | Tránh hai task đua nhau |
| D2 | Thêm `api.h` làm hợp đồng nội bộ | Code song song không đợi nhau |
| D3 | Kịch bản bơm triều giao P9; main.cpp/mutex/watchdog giao P1 | Bản gốc chưa giao |
| D4 | P7 đọc GPIO32/GPIO19 trong levelUpdate() | P7 dùng chúng để chặn bơm |
| D5 | `mode` = "predictive" khi có lệnh bật bơm còn hiệu lực từ backend, ngược lại "reactive" (ĐỐI CHIẾU HỢP ĐỒNG) | Bản gốc không nói |
| D6 | `run_id` lấy từ lệnh scenario, không có thì tự sinh `r<boot>-<số thứ tự>` (ĐỐI CHIẾU HỢP ĐỒNG) | Bản gốc chỉ nói "gán run_id" |
| D7 | >= 3/5 mẫu dùng được mới `valid` | Giả định tạm |
| D8 | Mẫu đầu tiên luôn được chấp nhận | Cách làm tạm |
| D9 | Bơm xả chỉ bị chặn bởi GPIO32, không bởi GPIO19 | GPIO19 chỉ cắt bơm triều; chờ xác nhận Q3 |
| D10 | PWM 1 kHz, LEDC 8 bit, duty 255 = 100% | Tạm, chỉnh theo tiếng bơm và dòng đo |
