#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void cspot_start(const char* device_name);
void cspot_stop(void);
bool cspot_is_running(void);

/* Decodes the embedded test_tone.ogg via Tremor into Tab5AudioSink.
 * Used to isolate: silent cspot playback = Tremor issue or CDNAudioFile? */
void tremor_selftest_run(void);

#ifdef __cplusplus
}
#endif
