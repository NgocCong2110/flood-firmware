// Hàm rỗng tạm để nạp thử một mình. Ai merge hàm thật vào thì XOÁ hàm đó khỏi file này.
#include "api.h"
void sampleCommit() {}                                  // P2
void scenarioTask(void*) { vTaskDelete(NULL); }         // P9
void netTask(void*)      { vTaskDelete(NULL); }         // P2
void eventPost(const char*, const char*, const char*) {}              // P2
void ackPost(const char*, bool, const char*) {}                       // P2
bool scenarioStart(const ScenarioReq&, const char** r) { *r = "invalid_cmd"; return false; }   // P9
bool scenarioStop(const char*, const char** r)         { *r = "invalid_cmd"; return false; }   // P9
void netInit() {}                                                     // P2
void eventPostState(const char*, const char*, const char*, float) {}  // P2
volatile uint32_t gNetBeat = 0;                                       // P2
