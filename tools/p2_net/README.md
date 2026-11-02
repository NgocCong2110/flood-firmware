# Công cụ MQTT của P2

Cài một lần: `pip install paho-mqtt`. Chạy từ thư mục gốc repo. `--host` là IP máy chạy Mosquitto.

| Script | Dùng khi | Lệnh |
|---|---|---|
| `fake_esp32.py` | Chưa có board: giả ESP32 gửi telemetry/status/event và trả ACK. Nhóm C dùng để thử backend. Mặc định `source:"sim"` | `python tools/p2_net/fake_esp32.py --host <IP>` |
| `fake_esp32.py` (giả mất mạng) | Thử phía backend: OFFLINE, gửi bù `replay:true` | `... --offline-at 60 --offline-for 300` |
| `send_tests.py` | Chạy ca thử lệnh (T01, T05–T21, T27) và so ACK với kỳ vọng. Dùng với board thật hoặc ESP32 giả | `python tools/p2_net/send_tests.py --host <IP>` |
| `check_seq.py` | Nghiệm thu #8: không thiếu bản sau khi mất Wi-Fi 5 phút | `python tools/p2_net/check_seq.py mat_wifi.log` |

Ghi log cho #8: `mosquitto_sub -h <IP> -t "flood/node01/#" -v > mat_wifi.log`

Trên board thật:
- Ca kịch bản (T10, T13–T15, T21) cần `scenario.cpp` thật của P9.
- T03/T04 ghi hệ số giả vào NVS nên mặc định bị bỏ qua (`--with-calib` để chạy, xong gửi lại hệ số thật).

Đã chạy thử với Mosquitto + `fake_esp32.py`: `send_tests.py` 27/27 đạt; giả mất mạng 8 s rồi `check_seq.py` báo không thiếu bản. Chưa chạy với board thật.
