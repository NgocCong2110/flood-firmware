#pragma once
// OWNER: P2
// Vòng đệm telemetry 900 bản + theo dõi "đã gửi / chưa gửi" để gửi bù khi mất mạng.
// Logic thuần C++ (không dùng Arduino), test trên PC: test/p2_net/test_p2.cpp.
// KHÔNG tự khoá: net.cpp bọc mọi lời gọi bằng mutex riêng.
// Hợp đồng v6.1 mục 6.2, 7, 7.5; quyết định #7, #8, #9.
#include <stdint.h>
#include <stddef.h>

// Một bản đo, ~56 byte, 900 bản ~50 KB RAM tĩnh.
struct Sample {
  uint32_t seq, boot, ts, ms;       // ts = 0 nếu lúc đo chưa có NTP; ms = millis() lúc đo
  float    raw_mm, level_mm, res_mm;
  int8_t   rssi;
  uint8_t  pump_tide_pct;
  uint8_t  riv_state;               // 0..3 = NORMAL..DANGER, 4 = SENSOR_ERROR
  uint8_t  res_state;               // 0..3 = R_OK..R_FULL,   4 = R_SENSOR_ERROR
  uint8_t  flags;
  char     run_id[12];
};

enum : uint8_t {
  F_VALID = 1, F_RES_VALID = 2, F_PUMP_ON = 4, F_RES_FULL = 8,
  F_RIV_OVER = 16, F_REPLAY = 32, F_PREDICTIVE = 64, F_RES_FLOAT_CUT = 128
};

// FIFO cơ bản. Đầy thì push() bỏ bản cũ nhất.
template <size_t N>
class RingBuf {
 public:
  bool push(const Sample& s) {            // true nếu phải bỏ bản cũ nhất
    bool dropped = false;
    if (count_ == N) { head_ = (head_ + 1) % N; count_--; dropped = true; }
    d_[(head_ + count_) % N] = s;
    count_++;
    return dropped;
  }
  void   pop()            { if (count_) { head_ = (head_ + 1) % N; count_--; } }
  size_t size() const     { return count_; }
  Sample& at(size_t i)    { return d_[(head_ + i) % N]; }   // i = 0 là bản cũ nhất
  const Sample& at(size_t i) const { return d_[(head_ + i) % N]; }
  static constexpr size_t capacity() { return N; }
 private:
  Sample d_[N];
  size_t head_ = 0, count_ = 0;
};

// Vòng đệm có đánh dấu đã gửi. Bố cục: [0, nsent) đã gửi (giữ lại phòng mất), [nsent, size) chưa gửi.
//
// Vì PubSubClient chỉ QoS 0 và TCP có thể "nửa chết" vài chục giây (publish vẫn trả true),
// bản đã gửi KHÔNG xoá ngay mà giữ KEEP_S giây; khi phát hiện mất liên kết thì các bản gửi trong
// RESEND_S giây gần nhất quay về "chưa gửi" và gắn F_REPLAY. Hệ quả: có thể có bản trùng,
// backend bỏ trùng theo (device_id, boot, seq).
template <size_t N>
class TxRing {
 public:
  static constexpr uint32_t KEEP_S   = 60;   // bản đã gửi quá 60 s thì xoá
  static constexpr uint32_t RESEND_S = 40;   // mất liên kết: gửi lại các bản đo trong 40 s gần nhất

  // Thêm bản đo mới. Trả true nếu phải bỏ một bản CHƯA gửi (mất thật -> BUFFER_OVERFLOW).
  bool push(const Sample& s) {
    bool lost = false;
    if (r_.size() == N) {
      if (nsent_ > 0) nsent_--;     // bản cũ nhất đã gửi: bỏ đi không mất gì
      else lost = true;             // bản cũ nhất chưa gửi: mất thật
    }
    r_.push(s);
    return lost;
  }

  // Lấy bản chưa gửi cũ nhất (không xoá). false nếu không còn.
  bool peekUnsent(Sample& out) const {
    if (nsent_ >= r_.size()) return false;
    out = r_.at(nsent_);
    return true;
  }

  // Đánh dấu bản vừa gửi là "đã gửi". Kiểm seq/boot để an toàn nếu vòng đệm đổi giữa peek và mark.
  void markSent(uint32_t boot, uint32_t seq) {
    if (nsent_ < r_.size() && r_.at(nsent_).seq == seq && r_.at(nsent_).boot == boot) nsent_++;
  }

  // Mất liên kết (Wi-Fi hoặc MQTT rớt) lúc now_ts.
  void onLinkLost(uint32_t now_ts) {
    size_t keep = 0;
    while (keep < nsent_ && now_ts > r_.at(keep).ts + RESEND_S) keep++;   // cũ hơn 40 s: coi như đã tới
    for (size_t i = keep; i < nsent_; i++) r_.at(i).flags |= F_REPLAY;
    nsent_ = keep;
  }

  // Xoá bản đã gửi quá KEEP_S giây.
  void purge(uint32_t now_ts) {
    while (nsent_ > 0 && now_ts > r_.at(0).ts + KEEP_S) { r_.pop(); nsent_--; }
  }

  // Vừa có NTP: quy đổi các bản ts = 0 theo millis(). ts = now - (millis() - ms)/1000.
  void fixTimestamps(uint32_t now_ts, uint32_t now_ms) {
    for (size_t i = 0; i < r_.size(); i++) {
      Sample& s = r_.at(i);
      if (s.ts == 0) s.ts = now_ts - (now_ms - s.ms) / 1000;
    }
  }

  size_t size() const     { return r_.size(); }
  size_t unsent() const   { return r_.size() - nsent_; }
  size_t sent() const     { return nsent_; }
  const Sample& at(size_t i) const { return r_.at(i); }
  static constexpr size_t capacity() { return N; }

 private:
  RingBuf<N> r_;
  size_t nsent_ = 0;
};
