# common/ - FILE DÙNG CHUNG (chủ: P1)

P1 tạo và push NGÀY 1 SÁNG. Sau đó cả nhóm pull, **không ai sửa nếu chưa báo cả nhóm**.

| File | Nội dung | Chép từ |
|---|---|---|
| `pins.h` | GPIO đúng hợp đồng mục 6, kênh LEDC | doc mục 5.3 |
| `api.h` | struct Config/SensorCal/Meas/Shared, mutex, khai báo hàm 4 người | doc mục 5.4 |
| `secrets.example.h` | mẫu WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, DEVICE_ID | doc mục 5.2 |
| `secrets.h` | mỗi người tự tạo, đã nằm trong .gitignore | - |

Quy tắc `Shared`: mỗi trường chỉ MỘT người ghi; ghi thì bọc Lock; không delay/publish/Serial dài khi giữ Lock.
