#pragma once
// Pin map ESP32 DevKit V1 (hợp đồng mục 6). Không dùng GPIO 0, 2, 12, 15.
// GPIO 34, 35 chỉ làm đầu vào: dùng cho Echo. Echo phải qua chia áp 1k/2k (5V -> 3,3V).
namespace pin {
constexpr int RIV_TRIG    = 27;   // JSN-SR04T sông
constexpr int RIV_ECHO    = 34;
constexpr int RES_TRIG    = 26;   // JSN-SR04T hồ
constexpr int RES_ECHO    = 35;
constexpr int PUMP_DRAIN  = 25;   // bơm xả (BTS7960 #1, RPWM): P7
constexpr int PUMP_TIDE   = 33;   // bơm triều (BTS7960 #2, RPWM): P9
constexpr int IL_RES_FULL = 32;   // đọc EN#1 (phao hồ). INPUT thường, KHÔNG pull-up. LOW = đã cắt
constexpr int IL_RIV_OVER = 19;   // đọc EN#2 (phao sông tràn). Như trên
constexpr int BUZZER      = 13;
constexpr int LED_G       = 16;
constexpr int LED_Y       = 17;
constexpr int LED_R       = 4;
}  // namespace pin

// Kênh LEDC (PWM). Tần số 1 kHz là TẠM, chưa chốt (hợp đồng mục 15).
constexpr int CH_DRAIN = 0;
constexpr int CH_TIDE  = 1;
