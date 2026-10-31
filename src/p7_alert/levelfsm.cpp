// OWNER: P7
// Máy trạng thái mức + hysteresis + SENSOR_ERROR.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.
#include "levelfsm.h"

static const char* RIV[5] = {"NORMAL", "WATCH", "WARNING", "DANGER", "SENSOR_ERROR"};
static const char* RES[5] = {"R_OK", "R_WATCH", "R_HIGH", "R_FULL", "R_SENSOR_ERROR"};
const char* LevelFsm::name(bool reservoir, int c) {
  if (c < 0 || c > 4) c = 4;
  return reservoir ? RES[c] : RIV[c];
}

int LevelFsm::rawLevel(float x) const {
  int r = 0;
  for (int i = 0; i < 3; i++) if (x >= t_[i]) r = i + 1;
  return r;
}

// Khi đã quyết định xuống: xuống tới mức cao nhất k < lvl_ mà x vẫn còn >= ngưỡng vào của k trừ hyst
int LevelFsm::downTarget(float x) const {
  for (int k = lvl_ - 1; k >= 1; k--)
    if (x >= t_[k - 1] - hyst_) return k;
  return 0;
}

FsmEvent LevelFsm::step(bool valid, float x) {
  FsmEvent ev{false, state(), state()};

  if (!valid) {                                   // mẫu hỏng
    good_ = 0; upN_ = 0; downN_ = 0;
    if (!err_ && ++bad_ >= 5) { err_ = true; ev = {true, lvl_, 4}; }
    return ev;
  }
  bad_ = 0;

  if (err_) {                                     // đang lỗi: cần 3 mẫu hợp lệ liên tiếp để thoát
    if (++good_ >= 3) {
      err_ = false; good_ = 0; upN_ = 0; downN_ = 0;
      int nl = rawLevel(x);
      ev = {true, 4, nl};
      lvl_ = nl;
    }
    return ev;                                    // trong lỗi: không báo mức
  }

  int r = rawLevel(x);
  if (r > lvl_) {                                 // ứng viên lên mức
    downN_ = 0;
    upMin_ = (upN_ == 0) ? r : (r < upMin_ ? r : upMin_);
    if (++upN_ >= 3) { ev = {true, lvl_, upMin_}; lvl_ = upMin_; upN_ = 0; }
  } else {
    upN_ = 0;
    if (lvl_ > 0 && x < t_[lvl_ - 1] - hyst_) {   // ứng viên xuống mức
      int tg = downTarget(x);
      downMax_ = (downN_ == 0) ? tg : (tg > downMax_ ? tg : downMax_);
      if (++downN_ >= 3) { ev = {true, lvl_, downMax_}; lvl_ = downMax_; downN_ = 0; }
    } else downN_ = 0;
  }
  return ev;
}