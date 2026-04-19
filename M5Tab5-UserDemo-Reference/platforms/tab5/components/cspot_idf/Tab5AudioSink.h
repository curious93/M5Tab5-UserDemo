#pragma once

#include "AudioSink.h"
#include <cstdint>
#include <cstdlib>

class Tab5AudioSink : public AudioSink {
public:
    Tab5AudioSink();
    ~Tab5AudioSink() override = default;

    void feedPCMFrames(const uint8_t* buffer, size_t bytes) override;
    void volumeChanged(uint16_t volume) override;
    bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override;

private:
    uint32_t _sampleRate = 44100;
    uint8_t  _bitDepth   = 16;
    uint8_t  _channels   = 2;

    // 44.1 → 48 kHz linear-interp upsampler state (Q16 phase)
    uint32_t _phase  = 0;
    int16_t  _prevL  = 0;
    int16_t  _prevR  = 0;
};
