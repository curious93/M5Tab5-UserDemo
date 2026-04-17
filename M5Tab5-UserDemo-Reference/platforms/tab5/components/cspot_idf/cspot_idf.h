#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void cspot_start(const char* device_name);
void cspot_stop(void);
bool cspot_is_running(void);

#ifdef __cplusplus
}
#endif
