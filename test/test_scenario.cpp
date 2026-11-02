#include <cstdio>
#include <cmath>
#include "scenario_calc.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL dong %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < 0.5f; }

int main() {
  // S01: sóng cơ bản
  ScenarioParams p{300, 50, 20, false, 0, 0, 0};
  CHECK(near(scenarioPwm(p, 0, 70), 50));
  CHECK(near(scenarioPwm(p, 75, 70), 70));    // đỉnh
  CHECK(near(scenarioPwm(p, 150, 70), 50));
  CHECK(near(scenarioPwm(p, 225, 70), 30));   // đáy

  // S02: đột biến ở pha thấp
  ScenarioParams s{300, 50, 20, true, 200, 20, 15};
  CHECK(near(scenarioPwm(s, 205, 70), 31.7f + 20));
  CHECK(near(scenarioPwm(s, 225, 70), 30));   // ngoài khoảng đột biến

  // S03: đột biến ở pha cao -> bị kẹp 70
  ScenarioParams h{300, 50, 20, true, 70, 20, 15};
  CHECK(near(scenarioPwm(h, 75, 70), 70));

  // S04: P0+P1 > pwm_max -> kẹp 70
  ScenarioParams big{300, 70, 20, false, 0, 0, 0};
  CHECK(near(scenarioPwm(big, 75, 70), 70));

  // S05: các trường hợp bị từ chối
  CHECK(scenarioValid(p, 20));
  ScenarioParams bad{300, 30, 20, false, 0, 0, 0};     // P0-P1 = 10 < 20
  CHECK(!scenarioValid(bad, 20));
  ScenarioParams bad2{300, 50, 20, true, 10, 20, 0};   // width = 0
  CHECK(!scenarioValid(bad2, 20));
  ScenarioParams bad3{300, NAN, 20, false, 0, 0, 0};   // không hữu hạn
  CHECK(!scenarioValid(bad3, 20));
  ScenarioParams bad4{9, 50, 20, false, 0, 0, 0};      // chu kỳ < 10
  CHECK(!scenarioValid(bad4, 20));
  ScenarioParams ok10{10, 50, 20, false, 0, 0, 0};     // đúng 10 vẫn hợp lệ
  CHECK(scenarioValid(ok10, 20));
  ScenarioParams bad5{300, 50, 20, true, -1, 20, 15};  // surge t < 0
  CHECK(!scenarioValid(bad5, 20));

  printf(fails ? "CO %d LOI\n" : "TAT CA DEU QUA\n", fails);
  return fails ? 1 : 0;
}