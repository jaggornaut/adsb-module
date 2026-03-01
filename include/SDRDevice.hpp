#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "SDRSettings.hpp"

namespace jsignal {

class SDRDevice {
public:
    using DataHandler = std::function<void(const uint8_t*, uint32_t)>;

    virtual ~SDRDevice() = default;

    virtual bool init(const SDRSettings& settings) = 0;

    virtual void startReceiving() = 0;
    virtual void stopReceiving() = 0;

    virtual std::pair<double, double> getRefPosition() const = 0;
};

}
