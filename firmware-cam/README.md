# FW2 - ESP32-CAM (P9)
Board AI Thinker. Chỉ làm camera: `/capture` cổng 80, `/stream` cổng 81, HTTP trong LAN, KHÔNG qua MQTT.
IP cố định; gửi IP cho P8 và nhóm C. Nghiệm thu #21: liên tục >= 10 phút, trễ <= 3 s, tự nối lại <= 30 s.
Có `secrets.h` riêng (không commit).
