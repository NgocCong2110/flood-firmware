// Hàm rỗng tạm để nạp thử một mình. Ai merge hàm thật vào thì XOÁ hàm đó khỏi file này.
#include "api.h"
void scenarioTask(void*) { vTaskDelete(NULL); }         // P9
bool scenarioStart(const ScenarioReq&, const char** r) { *r = "invalid_cmd"; return false; }   // P9
bool scenarioStop(const char*, const char** r)         { *r = "invalid_cmd"; return false; }   // P9
