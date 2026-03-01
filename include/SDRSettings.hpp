#pragma once

#include <cstdint>

namespace jsignal {

struct SDRSettings {
    int device_index;
    uint32_t center_frequency;
    uint32_t sample_rate;
    int tuner_gain;
    double ref_latitude;
    double ref_longitude;
};

}
