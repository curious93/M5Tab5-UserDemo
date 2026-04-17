#pragma once

#include "BellTask.h"
#include "SpircHandler.h"
#include "CircularBuffer.h"
#include "Tab5AudioSink.h"
#include <atomic>
#include <memory>

class CSpotPlayer : public bell::Task {
public:
    CSpotPlayer(std::shared_ptr<cspot::SpircHandler> handler);

private:
    std::shared_ptr<cspot::SpircHandler> _handler;
    std::unique_ptr<Tab5AudioSink>       _sink;
    std::unique_ptr<bell::CircularBuffer> _buf;
    std::atomic<bool> _paused{false};

    void feedData(uint8_t* data, size_t len);
    void runTask() override;
};

class CSpotTask : public bell::Task {
public:
    CSpotTask(const std::string& deviceName);

private:
    std::string _deviceName;
    void runTask() override;
};
