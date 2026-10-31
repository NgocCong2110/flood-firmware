# Câu hỏi mở (KHÔNG tự sửa hợp đồng)

| # | Vấn đề | Cách làm tạm | Hỏi ai | Trạng thái |
|---|---|---|---|---|
| Q1 | `cmd calibrate` không nói áp cảm biến nào | Thêm `"sensor":"river"/"reservoir"`, mặc định river | B và C | mở |
| Q2 | `status` là chuỗi trơn hay JSON | LWT chuỗi `offline`; online gửi JSON {fw, uptime} | C | mở |
| Q3 | GPIO19/GPIO32 có chặn bơm xả không | Chỉ GPIO32 (D9) | Trưởng nhóm | mở |
| Q4 | Số mẫu hợp lệ tối thiểu | >= 3/5 (D7) | Trưởng nhóm | mở |
| Q5 | `valid` khi chưa có bản trước | Chấp nhận mẫu đầu (D8) | Trưởng nhóm | mở |
| Q6 | `jump_mm` = 50 có thấp khi surge mạnh | Chốt sau N2 bằng số đo | A + P1 | mở |
| Q7 | Tần số PWM bơm | 1 kHz (D10) | A | mở |
| Q8 | Tên topic/trường của event, ack, scenario, config | Theo file P2/P9, đối chiếu hợp đồng mục 7 | C | mở |
