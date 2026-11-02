// Test P2 trên PC (không cần board): vòng đệm gửi bù + kiểm cấu trúc lệnh MQTT.
// Cần ArduinoJson 7 (header-only). Sau khi `pio run` một lần, thư viện nằm ở .pio/libdeps/esp32dev/ArduinoJson/src
//   g++ -std=c++17 -Wall -Isrc/p2_net -I.pio/libdeps/esp32dev/ArduinoJson/src test/p2_net/test_p2.cpp -o t && ./t
// Hoặc tải ArduinoJson về (git clone https://github.com/bblanchon/ArduinoJson) rồi trỏ -I tới thư mục src của nó.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "ringbuf.h"
#include "netcmd.h"

static int gFail = 0, gPass = 0;
#define CHECK(name, cond) do { if (cond) gPass++; else { gFail++; std::printf("FAIL %s (dong %d)\n", name, __LINE__); } } while (0)

using namespace netcmd;

static Decoded cmd(const char* js, int ttl = 30) {
  JsonDocument d;
  if (deserializeJson(d, js)) { Decoded x; return x; }   // net.cpp: JSON hỏng -> ACK id "", invalid_cmd
  return decodeCmd(d.as<JsonVariantConst>(), ttl);
}
static Decoded scn(const char* js) {
  JsonDocument d;
  if (deserializeJson(d, js)) { Decoded x; return x; }
  return decodeScenario(d.as<JsonVariantConst>());
}

struct Cfg {   // giống struct Config trong api.h (mặc định hợp đồng mục 7)
  float L1 = 80, L2 = 120, L3 = 160, hyst = 10, pump_off = 110, H_max = 200;
  float R_watch = 120, R_high = 160, R_full = 180, jump_mm = 50;
  int32_t pwm_min = 20, pwm_max = 70, cmd_ttl_s = 30;
  float r2_min = 0.85f;
  int32_t pre_alert_s = 60, pre_drain_s = 30, fc_stale_s = 10;
};
static bool cfg(const char* js, Cfg& c) {
  JsonDocument d;
  if (deserializeJson(d, js)) return false;
  return applyConfig(d.as<JsonVariantConst>(), c);
}

static Sample mk(uint32_t seq, uint32_t ts) { Sample s{}; s.seq = seq; s.boot = 1; s.ts = ts; return s; }

static void testRing() {
  { RingBuf<4> r; Sample s{};
    for (uint32_t i = 1; i <= 4; i++) { s.seq = i; CHECK("ring push", !r.push(s)); }
    s.seq = 5; CHECK("ring day bo ban cu", r.push(s));
    CHECK("ring thu tu", r.size() == 4 && r.at(0).seq == 2 && r.at(3).seq == 5);
    r.pop(); CHECK("ring pop", r.at(0).seq == 3); }

  // Gửi bình thường: đánh dấu đã gửi, chưa xoá; quá 60 s mới xoá
  { static TxRing<10> t; Sample x;
    for (uint32_t i = 1; i <= 3; i++) t.push(mk(i, 1000 + i));
    while (t.peekUnsent(x)) t.markSent(x.boot, x.seq);
    CHECK("gui xong chua xoa", t.size() == 3 && t.unsent() == 0);
    t.purge(1050); CHECK("chua qua 60 s", t.size() == 3);
    t.purge(1062); CHECK("qua 60 s xoa ban 1", t.size() == 2 && t.at(0).seq == 2); }

  // Mất liên kết: bản gửi trong 40 s gần nhất quay về chưa gửi, gắn F_REPLAY, đúng thứ tự
  { static TxRing<100> t; Sample x;
    for (uint32_t i = 1; i <= 50; i++) t.push(mk(i, 1000 + i));       // ts 1001..1050
    while (t.peekUnsent(x)) t.markSent(x.boot, x.seq);
    t.onLinkLost(1050);                                                // giữ bản ts < 1010 (đã quá 40 s)
    CHECK("link lost: so ban gui lai", t.unsent() == 41);
    t.peekUnsent(x); CHECK("link lost: ban dau tien", x.seq == 10 && (x.flags & F_REPLAY));
    CHECK("ban cu khong gan replay", !(t.at(0).flags & F_REPLAY));
    for (uint32_t i = 51; i <= 60; i++) t.push(mk(i, 1000 + i));       // đo tiếp lúc mất mạng
    uint32_t prev = 0; bool inc = true; int n = 0;
    while (t.peekUnsent(x)) { if (x.seq <= prev) inc = false; prev = x.seq; t.markSent(x.boot, x.seq); n++; }
    CHECK("gui bu dung thu tu, khong thieu", inc && n == 51 && prev == 60); }

  // Tràn: bỏ bản đã gửi trước, không báo mất; chỉ báo mất khi phải bỏ bản chưa gửi
  { static TxRing<5> t; Sample x;
    for (uint32_t i = 1; i <= 3; i++) t.push(mk(i, 1000 + i));
    while (t.peekUnsent(x)) t.markSent(x.boot, x.seq);                 // 3 đã gửi
    t.push(mk(4, 1004)); t.push(mk(5, 1005));
    CHECK("tran bo ban da gui: khong mat", !t.push(mk(6, 1006)) && t.sent() == 2 && t.unsent() == 3);
    CHECK("tran 2", !t.push(mk(7, 1007)) && !t.push(mk(8, 1008)) && t.sent() == 0);
    CHECK("tran bo ban chua gui: mat that", t.push(mk(9, 1009)));
    t.peekUnsent(x); CHECK("sau tran ban cu nhat", x.seq == 5); }

  // Chưa có NTP: ts = 0, quy đổi theo millis
  { static TxRing<5> t; Sample s = mk(1, 0); s.ms = 10000; t.push(s);
    t.fixTimestamps(1730000000, 25000);
    CHECK("quy doi ts theo millis", t.at(0).ts == 1730000000 - 15); }

  // markSent sai seq không làm lệch con trỏ
  { static TxRing<5> t; t.push(mk(1, 1001)); t.markSent(1, 99);
    CHECK("markSent sai seq bo qua", t.unsent() == 1); }
}

static void testCmd() {
  Decoded d;
  // calibrate (T03–T09)
  d = cmd("{\"id\":\"cal-001\",\"calibrate\":{\"sensor\":\"river\",\"a\":-1.00284,\"b\":501.10}}");
  CHECK("T03 calibrate river", d.act == Act::Calibrate && !strcmp(d.id, "cal-001") && !strcmp(d.sensor, "river") && d.b > 501 && d.b < 501.2f);
  d = cmd("{\"id\":\"cal-002\",\"calibrate\":{\"sensor\":\"reservoir\",\"a\":-1,\"b\":500}}");
  CHECK("T04 calibrate reservoir (so nguyen)", d.act == Act::Calibrate && !strcmp(d.sensor, "reservoir") && d.a == -1);
  d = cmd("{\"id\":\"cal-003\",\"calibrate\":{\"a\":-1,\"b\":500}}");
  CHECK("T05 thieu sensor", d.act == Act::Reject && !strcmp(d.id, "cal-003"));
  CHECK("T06 sensor lake", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"lake\",\"a\":-1,\"b\":500}}").act == Act::Reject);
  CHECK("T06b sensor viet hoa", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"River\",\"a\":-1,\"b\":500}}").act == Act::Reject);
  CHECK("T07 sensor ngoai calibrate", cmd("{\"id\":\"c\",\"sensor\":\"river\",\"calibrate\":{\"a\":-1,\"b\":500}}").act == Act::Reject);
  CHECK("T08 thieu b", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"river\",\"a\":-1}}").act == Act::Reject);
  CHECK("T08 a la chuoi", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"river\",\"a\":\"-1\",\"b\":500}}").act == Act::Reject);
  CHECK("T08 a la bool", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"river\",\"a\":true,\"b\":500}}").act == Act::Reject);
  CHECK("calibrate khong phai object", cmd("{\"id\":\"c\",\"calibrate\":5}").act == Act::Reject);
  // T09/T27 (a = 0, a = 6, b = 3000): P2 cho qua, P1 calibrateApply từ chối -> ACK invalid_cmd (kiểm trên board)
  CHECK("T09 a=0 P2 chuyen cho P1", cmd("{\"id\":\"c\",\"calibrate\":{\"sensor\":\"river\",\"a\":0,\"b\":500}}").act == Act::Calibrate);

  // mode
  d = cmd("{\"id\":\"c2\",\"mode\":\"predictive\"}");
  CHECK("mode predictive", d.act == Act::Mode && d.predictive);
  CHECK("mode reactive", cmd("{\"id\":\"c3\",\"mode\":\"reactive\"}").act == Act::Mode);
  CHECK("mode la", cmd("{\"id\":\"c4\",\"mode\":\"auto\"}").act == Act::Reject);

  // pump
  d = cmd("{\"id\":\"c1\",\"pump_drain\":\"on\",\"ttl_s\":30}");
  CHECK("pump on", d.act == Act::Pump && !strcmp(d.pump_val, "on") && d.ttl_s == 30);
  CHECK("pump ttl mac dinh", cmd("{\"id\":\"c1\",\"pump_drain\":\"off\"}", 25).ttl_s == 25);
  CHECK("pump ttl kep 300", cmd("{\"id\":\"c1\",\"pump_drain\":\"on\",\"ttl_s\":999}").ttl_s == 300);
  CHECK("pump ttl kep 1", cmd("{\"id\":\"c1\",\"pump_drain\":\"on\",\"ttl_s\":0}").ttl_s == 1);
  CHECK("pump ttl chuoi", cmd("{\"id\":\"c1\",\"pump_drain\":\"on\",\"ttl_s\":\"30\"}").act == Act::Reject);
  CHECK("pump gia tri khong phai chuoi", cmd("{\"id\":\"c1\",\"pump_drain\":1}").act == Act::Reject);
  CHECK("pump gia tri la -> P7 tu tu choi", cmd("{\"id\":\"c1\",\"pump_drain\":\"maybe\"}").act == Act::Pump);

  // id, nhiều hành động, JSON hỏng (T17–T19)
  d = cmd("{\"pump_drain\":\"on\"}");
  CHECK("T12 thieu id -> id rong", d.act == Act::Reject && d.id[0] == 0);
  CHECK("T17 JSON hong", cmd("{\"id\":\"c1\",").act == Act::Reject);
  CHECK("T18 id 20 ky tu", cmd("{\"id\":\"abcdefghij0123456789\",\"mode\":\"reactive\"}").act == Act::Reject);
  CHECK("T18 id 19 ky tu duoc", cmd("{\"id\":\"abcdefghij012345678\",\"mode\":\"reactive\"}").act == Act::Mode);
  d = cmd("{\"id\":\"a\\\"b\",\"mode\":\"reactive\"}");
  CHECK("T18 id co dau nhay", d.act == Act::Reject && d.id[0] == 0);
  CHECK("id la so", cmd("{\"id\":5,\"mode\":\"reactive\"}").act == Act::Reject);
  d = cmd("{\"id\":\"c9\",\"mode\":\"reactive\",\"pump_drain\":\"on\"}");
  CHECK("T19 hai hanh dong", d.act == Act::Reject && !strcmp(d.id, "c9"));
  CHECK("khong co hanh dong", cmd("{\"id\":\"c9\"}").act == Act::Reject);
  CHECK("root la mang", cmd("[1,2]").act == Act::Reject);
}

static void testScenario() {
  Decoded d;
  d = scn("{\"id\":\"scn-start-001\",\"scenario\":\"start\",\"run_id\":\"run-001\",\"period_s\":300,\"P0\":50,\"P1\":20}");
  CHECK("T10 start du truong", d.act == Act::ScnStart && !strcmp(d.id, "scn-start-001") && !strcmp(d.run_id, "run-001") &&
                               d.period_s == 300 && d.P0 == 50 && d.P1 == 20 && !d.has_surge);
  CHECK("T16 id khac run_id", strcmp(d.id, d.run_id) != 0);
  d = scn("{\"id\":\"s2\",\"scenario\":\"start\",\"run_id\":\"r2\",\"period_s\":300,\"P0\":50,\"P1\":20,\"surge\":{\"t\":200,\"amp\":20,\"width\":15}}");
  CHECK("start co surge", d.act == Act::ScnStart && d.has_surge && d.s_t == 200 && d.s_amp == 20 && d.s_w == 15);
  CHECK("surge thieu width", scn("{\"id\":\"s2\",\"scenario\":\"start\",\"run_id\":\"r2\",\"period_s\":300,\"P0\":50,\"P1\":20,\"surge\":{\"t\":200,\"amp\":20}}").act == Act::Reject);
  CHECK("T11 thieu run_id", scn("{\"id\":\"s3\",\"scenario\":\"start\",\"period_s\":300,\"P0\":50,\"P1\":20}").act == Act::Reject);
  CHECK("T11 thieu period_s", scn("{\"id\":\"s3\",\"scenario\":\"start\",\"run_id\":\"r\",\"P0\":50,\"P1\":20}").act == Act::Reject);
  CHECK("T11 thieu P0", scn("{\"id\":\"s3\",\"scenario\":\"start\",\"run_id\":\"r\",\"period_s\":300,\"P1\":20}").act == Act::Reject);
  CHECK("T11 P1 la chuoi", scn("{\"id\":\"s3\",\"scenario\":\"start\",\"run_id\":\"r\",\"period_s\":300,\"P0\":50,\"P1\":\"20\"}").act == Act::Reject);
  d = scn("{\"scenario\":\"start\",\"run_id\":\"r\",\"period_s\":300,\"P0\":50,\"P1\":20}");
  CHECK("T12 start thieu id", d.act == Act::Reject && d.id[0] == 0);
  d = scn("{\"id\":\"scn-stop-001\",\"scenario\":\"stop\",\"run_id\":\"run-001\"}");
  CHECK("T13 stop", d.act == Act::ScnStop && !strcmp(d.id, "scn-stop-001") && !strcmp(d.run_id, "run-001"));
  CHECK("T14 stop run_id khac van chuyen cho P9", scn("{\"id\":\"x\",\"scenario\":\"stop\",\"run_id\":\"other\"}").act == Act::ScnStop);
  CHECK("stop thieu run_id", scn("{\"id\":\"x\",\"scenario\":\"stop\"}").act == Act::Reject);
  CHECK("run_id 12 ky tu", scn("{\"id\":\"x\",\"scenario\":\"stop\",\"run_id\":\"abcdefghijkl\"}").act == Act::Reject);
  CHECK("run_id 11 ky tu duoc", scn("{\"id\":\"x\",\"scenario\":\"stop\",\"run_id\":\"abcdefghijk\"}").act == Act::ScnStop);
  CHECK("scenario la", scn("{\"id\":\"x\",\"scenario\":\"pause\",\"run_id\":\"r\"}").act == Act::Reject);
  // T21 (clamped) do P9 trả: P2 chỉ chuyển giá trị
  CHECK("T21 P0=70 P1=20 chuyen P9", scn("{\"id\":\"x\",\"scenario\":\"start\",\"run_id\":\"r\",\"period_s\":300,\"P0\":70,\"P1\":20}").act == Act::ScnStart);
}

static void testConfig() {
  Cfg c;
  CHECK("config day du mac dinh", cfg("{\"L1\":80,\"L2\":120,\"L3\":160,\"hyst\":10,\"pump_off\":110,\"H_max\":200,\"R_watch\":120,\"R_high\":160,\"R_full\":180,\"jump_mm\":50,\"pwm_min\":20,\"pwm_max\":70,\"cmd_ttl_s\":30,\"r2_min\":0.85,\"pre_alert_s\":60,\"pre_drain_s\":30,\"fc_stale_s\":10}", c));
  CHECK("config ghi de 1 truong", cfg("{\"L1\":70}", c) && c.L1 == 70 && c.L2 == 120);
  CHECK("T26 L1 >= L2 bo qua", !cfg("{\"L1\":130}", c) && c.L1 == 70);
  CHECK("pwm_max > 100 bo qua", !cfg("{\"pwm_max\":120}", c) && c.pwm_max == 70);
  CHECK("pwm_min > pwm_max bo qua", !cfg("{\"pwm_min\":80}", c) && c.pwm_min == 20);
  CHECK("truong sai kieu bi lo", cfg("{\"L2\":\"abc\"}", c) && c.L2 == 120);
  CHECK("config la mang", !cfg("[]", c));
}

int main() {
  testRing(); testCmd(); testScenario(); testConfig();
  std::printf("%d dat, %d truot\n", gPass, gFail);
  return gFail ? 1 : 0;
}
