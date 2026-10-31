// Hàm rỗng tạm để nạp thử một mình. Ai merge hàm thật vào thì XOÁ hàm đó khỏi file này.
#include "api.h"
void levelUpdate() {}                                   // P7
void sampleCommit() {}                                  // P2
void pumpTask(void*)     { vTaskDelete(NULL); }         // P7
void scenarioTask(void*) { vTaskDelete(NULL); }         // P9
void uiTask(void*)       { vTaskDelete(NULL); }         // P7
void netTask(void*)      { vTaskDelete(NULL); }         // P2
void eventPost(const char*, const char*, const char*) {}              // P2
void ackPost(const char*, bool, const char*) {}                       // P2
void pumpOnCommand(const char*, const char*, int) {}                  // P7
bool scenarioStart(const ScenarioReq&, const char** r) { *r = "invalid_cmd"; return false; }   // P9
bool scenarioStop(const char*, const char** r)         { *r = "invalid_cmd"; return false; }   // P9
void netInit() {}                                                     // P2
void eventPostState(const char*, const char*, const char*, float) {}  // P2
volatile uint32_t gNetBeat = 0;                                       // P2
