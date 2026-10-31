// OWNER: P7
// Khai báo LevelFsm.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.
#pragma once
struct FsmEvent { bool changed; int from; int to; };   // mã trạng thái 0..4

class LevelFsm {
 public:
  void setThresholds(float t1, float t2, float t3, float hyst) {
    t_[0] = t1; t_[1] = t2; t_[2] = t3; hyst_ = hyst;
  }
  FsmEvent step(bool valid, float x);        // gọi 1 lần mỗi mẫu
  int  state() const { return err_ ? 4 : lvl_; }
  int  level() const { return lvl_; }
  bool error() const { return err_; }
  static const char* name(bool reservoir, int code);
 private:
  int rawLevel(float x) const;               // mức theo ngưỡng vào, không hysteresis
  int downTarget(float x) const;
  float t_[3] = {80, 120, 160}, hyst_ = 10;
  int lvl_ = 0;
  bool err_ = false;
  int bad_ = 0, good_ = 0, upN_ = 0, downN_ = 0, upMin_ = 0, downMax_ = 0;
};
