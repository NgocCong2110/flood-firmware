// OWNER: P7
// Các ca test cho LevelFsm và logic bơm xả.
// Chưa có code. Đọc README.md cùng thư mục trước khi viết.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "levelfsm.h"
#include "pumplogic.h"

static int fails = 0;
#define CHECK(c)                                  \
  do                                              \
  {                                               \
    if (!(c))                                     \
    {                                             \
      printf("FAIL dong %d: %s\n", __LINE__, #c); \
      fails++;                                    \
    }                                             \
  } while (0)

static void feed(LevelFsm &f, float x, int n, FsmEvent *last = nullptr)
{
  for (int i = 0; i < n; i++)
  {
    FsmEvent e = f.step(true, x);
    if (last)
      *last = e;
  }
}
static PumpIn base()
{
  PumpIn p{};
  p.riv_lvl = 3;
  p.level_mm = 165;
  p.pump_off = 110;
  p.res_valid = true;
  return p;
}

int main()
{
  // ---- LevelFsm sông: 80/120/160, hyst 10
  LevelFsm f;
  f.setThresholds(80, 120, 160, 10);
  feed(f, 70, 5);
  CHECK(f.state() == 0);
  feed(f, 85, 2);
  feed(f, 70, 1);
  CHECK(f.state() == 0); // nhiễu 2 mẫu bị bỏ qua
  FsmEvent e;
  feed(f, 85, 3, &e);
  CHECK(f.state() == 1 && e.changed && e.from == 0 && e.to == 1);
  feed(f, 130, 3);
  CHECK(f.state() == 2);
  feed(f, 165, 3);
  CHECK(f.state() == 3);
  feed(f, 155, 10);
  CHECK(f.state() == 3); // chưa xuống: 155 >= 150
  feed(f, 149, 3);
  CHECK(f.state() == 2); // < 150 → xuống 2
  feed(f, 105, 3);
  CHECK(f.state() == 1); // < 110 → xuống 1
  feed(f, 75, 10);
  CHECK(f.state() == 1); // hysteresis: 75 >= 70
  feed(f, 60, 3);
  CHECK(f.state() == 0);

  // ---- lỗi cảm biến
  LevelFsm g;
  g.setThresholds(80, 120, 160, 10);
  feed(g, 130, 3);
  for (int i = 0; i < 4; i++)
    g.step(false, 0);
  CHECK(!g.error()); // mới 4 lần
  g.step(true, 130); // 1 mẫu tốt reset đếm lỗi
  for (int i = 0; i < 4; i++)
    g.step(false, 0);
  CHECK(!g.error());
  FsmEvent ee = g.step(false, 0);
  CHECK(g.error() && ee.changed && ee.to == 4); // lần thứ 5 liên tiếp
  g.step(true, 130);
  g.step(true, 130);
  CHECK(g.error()); // mới 2 mẫu tốt
  g.step(false, 0);
  g.step(true, 130);
  g.step(true, 130);
  CHECK(g.error()); // lỗi xen reset
  FsmEvent ex = g.step(true, 130);
  CHECK(!g.error() && ex.changed && ex.from == 4 && ex.to == 2);

  // ---- PumpLogic REACTIVE
  PumpLogic L;
  PumpIn in = base();
  L.step(in, 0);
  CHECK(L.on);
  in.level_mm = 120;
  in.riv_lvl = 2;
  L.step(in, 500);
  CHECK(L.on); // vẫn chạy tới 110
  in.level_mm = 109;
  L.step(in, 1000);
  CHECK(!L.on && !L.reactive);
  in.level_mm = 165;
  in.riv_lvl = 3;
  L.step(in, 2000);
  CHECK(!L.on); // < 5 s kể từ lần tắt
  L.step(in, 5999);
  CHECK(!L.on);
  L.step(in, 6000);
  CHECK(L.on);
  in.res_sensor_full = true;
  const char *b = L.step(in, 6100);
  CHECK(b && !strcmp(b, "reservoir_full") && !L.on);
  in.res_sensor_full = false;
  L.step(in, 12000);
  CHECK(L.on);
  in.riv_err = true;
  b = L.step(in, 12100);
  CHECK(b && !strcmp(b, "sensor_error") && !L.on);

  // ---- PumpLogic lệnh PREDICTIVE
  PumpLogic M;
  PumpIn q = base();
  q.level_mm = 100;
  q.riv_lvl = 1;
  CHECK(!strcmp(M.request(q, true, 30, 0), "below_pump_off"));
  q.level_mm = 140;
  q.riv_lvl = 2;
  CHECK(!strcmp(M.request(q, true, 30, 0), "") && M.cmd);
  M.step(q, 1000);
  CHECK(M.on);
  M.step(q, 29999);
  CHECK(M.on);
  M.step(q, 30000);
  CHECK(!M.on && !M.cmd); // hết TTL → rơi về REACTIVE (không bật vì lvl 2)
  PumpIn w = base();
  w.gpio_res_cut = true;
  w.res_sensor_full = false;
  CHECK(!strcmp(pumpBlockReason(w), "interlock_off"));
  w.res_sensor_full = true;
  CHECK(!strcmp(pumpBlockReason(w), "reservoir_full"));

  // ---- Bảng 6.1 hợp đồng v6 (P7-T01..T07): GPIO32 x cảm biến hồ
  auto reason = [](bool low, bool full, bool err, bool rivErr = false)
  {
    PumpIn x = base();
    x.gpio_res_cut = low;
    x.res_sensor_full = full;
    x.res_err = err;
    x.riv_err = rivErr;
    return std::string(pumpBlockReason(x));
  };
  CHECK(reason(false, false, false) == "");              // T01
  CHECK(reason(true, true, false) == "reservoir_full");  // T02
  CHECK(reason(true, false, false) == "interlock_off");  // T03
  CHECK(reason(true, false, true) == "interlock_off");   // T04 (phần SENSOR_ERROR: LevelFsm bên dưới)
  CHECK(reason(false, true, false) == "reservoir_full"); // T05
  CHECK(reason(false, false, true) == "interlock_off");  // T06 (v6.1, tạm, chờ xác nhận): hồ R_SENSOR_ERROR
  {
    PumpIn x = base();
    x.res_valid = false;
    CHECK(std::string(pumpBlockReason(x)) == "interlock_off");
  } // T06: mẫu hồ không hợp lệ
  CHECK(reason(true, true, false, true) == "sensor_error"); // T07
  CHECK(reason(false, false, false, true) == "sensor_error");
  LevelFsm rf;
  rf.setThresholds(120, 160, 180, 10);
  feed(rf, 100, 3);
  for (int i = 0; i < 4; i++)
    rf.step(false, 0);
  CHECK(!rf.error());
  rf.step(false, 0);
  CHECK(rf.error() && rf.state() == 4); // T04: hồ vào R_SENSOR_ERROR ở mẫu hỏng thứ 5

  // ---- T08: đang chạy rồi GPIO32 LOW → tắt NGAY, một lần báo, không bao giờ bật lại khi còn LOW
  PumpLogic N;
  PumpIn n = base();
  N.step(n, 0);
  CHECK(N.on);
  n.gpio_res_cut = true;
  const char *nb = N.step(n, 100);
  CHECK(nb && !strcmp(nb, "interlock_off") && !N.on);
  for (uint32_t tt = 200; tt < 20000; tt += 100)
  {
    CHECK(N.step(n, tt) == nullptr);
    CHECK(!N.on);
  }
  CHECK(!strcmp(N.request(n, true, 30, 20000), "interlock_off"));
  // ---- T09: GPIO32 HIGH lại → bật lại sau >= 5 s kể từ lần tắt (tắt lúc 100)
  n.gpio_res_cut = false;
  N.step(n, 20000);
  CHECK(N.on);
  PumpLogic N2;
  PumpIn n2 = base();
  N2.step(n2, 0);
  n2.gpio_res_cut = true;
  N2.step(n2, 100);
  n2.gpio_res_cut = false;
  N2.step(n2, 4000);
  CHECK(!N2.on); // mới 3,9 s
  N2.step(n2, 5100);
  CHECK(N2.on);
  // ---- T06 động: đang chạy, GPIO32 HIGH, hồ lỗi → tắt ngay; hết lỗi → bật lại sau >= 5 s
  PumpLogic H;
  PumpIn h = base();
  H.step(h, 0);
  CHECK(H.on);
  h.res_valid = false;
  const char *hb = H.step(h, 100);
  CHECK(hb && !strcmp(hb, "interlock_off") && !H.on);
  CHECK(!strcmp(H.request(h, true, 30, 200), "interlock_off"));
  h.res_valid = true;
  H.step(h, 4000);
  CHECK(!H.on); // mới 3,9 s kể từ lần tắt
  H.step(h, 5100);
  CHECK(H.on);
  // ---- T10: lệnh bật khi mực < pump_off
  PumpLogic N3;
  PumpIn n3 = base();
  n3.level_mm = 100;
  n3.riv_lvl = 1;
  CHECK(!strcmp(N3.request(n3, true, 30, 0), "below_pump_off"));
  // ---- TTL: phạm vi hợp lệ và không thay đổi trạng thái khi sai
  {
    PumpIn ti = base();
    ti.riv_lvl = 2;
    ti.level_mm = 140; // chưa kích hoạt REACTIVE

    PumpLogic t;

    CHECK(!strcmp(t.request(ti, true, 0, 0), "invalid_cmd"));
    CHECK(!strcmp(t.request(ti, true, -1, 0), "invalid_cmd"));
    CHECK(!strcmp(t.request(ti, true, 301, 0), "invalid_cmd"));
    CHECK(!t.cmd && !t.on);

    // Biên dưới: 1 giây
    CHECK(!strcmp(t.request(ti, true, 1, 0), ""));
    t.step(ti, 0);
    CHECK(t.on);
    t.step(ti, 999);
    CHECK(t.on && t.cmd);
    t.step(ti, 1000);
    CHECK(!t.on && !t.cmd);

    // Biên trên: 300 giây
    PumpLogic upper;
    CHECK(!strcmp(upper.request(ti, true, 300, 0), ""));
    upper.step(ti, 299999);
    CHECK(upper.on && upper.cmd);
    upper.step(ti, 300000);
    CHECK(!upper.on && !upper.cmd);

    // Lệnh sai không được thay đổi yêu cầu hợp lệ trước đó
    PumpLogic keep;
    CHECK(!strcmp(keep.request(ti, true, 30, 0), ""));
    CHECK(!strcmp(keep.request(ti, true, 301, 10000),
                  "invalid_cmd"));
    CHECK(keep.cmd && keep.cmd_until == 30000);

    // Gia hạn: nhận lại ở giây 10 thì hết hạn ở giây 40
    PumpLogic renew;
    CHECK(!strcmp(renew.request(ti, true, 30, 0), ""));
    renew.step(ti, 0);
    CHECK(!strcmp(renew.request(ti, true, 30, 10000), ""));
    renew.step(ti, 30000);
    CHECK(renew.on && renew.cmd);
    renew.step(ti, 40000);
    CHECK(!renew.on && !renew.cmd);

    // OFF không phụ thuộc TTL
    PumpLogic stop;
    CHECK(!strcmp(stop.request(ti, true, 30, 0), ""));
    stop.step(ti, 0);
    CHECK(stop.on);
    CHECK(!strcmp(stop.request(ti, false, 0, 100), ""));
    stop.step(ti, 100);
    CHECK(!stop.on && !stop.cmd);
  }
    // ---- Mức hồ: 120 / 160 / 180 mm, hysteresis 10 mm
  {
    LevelFsm lake;
    lake.setThresholds(120, 160, 180, 10);

    feed(lake, 100, 3);
    CHECK(lake.state() == 0);  // R_OK

    // Hai mẫu vượt ngưỡng rồi giảm lại: không đổi mức
    feed(lake, 125, 2);
    CHECK(lake.state() == 0);
    feed(lake, 100, 1);
    CHECK(lake.state() == 0);

    // Tăng qua các mức
    feed(lake, 125, 3);
    CHECK(lake.state() == 1);  // R_WATCH

    feed(lake, 165, 3);
    CHECK(lake.state() == 2);  // R_HIGH

    feed(lake, 185, 3);
    CHECK(lake.state() == 3);  // R_FULL

    // Đang R_FULL: phải dưới 170 mm mới xét hạ mức
    feed(lake, 175, 3);
    CHECK(lake.state() == 3);
    feed(lake, 170, 3);
    CHECK(lake.state() == 3);

    feed(lake, 169, 2);
    CHECK(lake.state() == 3);
    feed(lake, 169, 1);
    CHECK(lake.state() == 2);  // R_HIGH

    // Đang R_HIGH: phải dưới 150 mm
    feed(lake, 150, 3);
    CHECK(lake.state() == 2);
    feed(lake, 149, 3);
    CHECK(lake.state() == 1);  // R_WATCH

    // Đang R_WATCH: phải dưới 110 mm
    feed(lake, 110, 3);
    CHECK(lake.state() == 1);
    feed(lake, 109, 3);
    CHECK(lake.state() == 0);  // R_OK

    // Đúng ngưỡng vào: vẫn cần ba mẫu
    LevelFsm edge;
    edge.setThresholds(120, 160, 180, 10);

    feed(edge, 120, 3);
    CHECK(edge.state() == 1);
    feed(edge, 160, 3);
    CHECK(edge.state() == 2);
    feed(edge, 180, 3);
    CHECK(edge.state() == 3);
  }
  printf(fails ? "CO %d LOI\n" : "TAT CA DEU QUA\n", fails);
  return fails ? 1 : 0;
}