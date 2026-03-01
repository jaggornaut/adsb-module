#include "RTLSDRDevice.hpp"
#include <rtl-sdr.h>
#include <iostream>

namespace jsignal {

void RTLSDRDevice::callback(uint8_t *buf, uint32_t len, void *ctx) {
    auto* self = static_cast<RTLSDRDevice*>(ctx);

    if (!self || !self->m_isReceiving || !self->m_queue) {
        return;
    }

    DataBuffer data_buffer(buf, buf + len);
    self->m_queue->push(std::move(data_buffer));
}

RTLSDRDevice::RTLSDRDevice()
    : m_dev(nullptr, rtlsdr_close),
      m_isReceiving(false),
      m_queue(nullptr)
{}

RTLSDRDevice::~RTLSDRDevice() {
    stopReceiving();
}

bool RTLSDRDevice::init(const SDRSettings& settings) {
    try {
        rtlsdr_dev_t *raw_dev = nullptr;
        if (rtlsdr_open(&raw_dev, settings.device_index) < 0) {
            std::cerr << "Error: Could not open RTLSDR device (index "
                      << settings.device_index << ")." << std::endl;
            return false;
        }
        m_dev.reset(raw_dev);

        rtlsdr_set_center_freq(m_dev.get(), settings.center_frequency);
        rtlsdr_set_sample_rate(m_dev.get(), settings.sample_rate);

        uint32_t real_freq = rtlsdr_get_center_freq(m_dev.get());
        uint32_t real_rate = rtlsdr_get_sample_rate(m_dev.get());
        std::cout << "SDR: Frequency: " << real_freq / 1e6 << " MHz" << std::endl;
        std::cout << "SDR: Sample rate: " << real_rate / 1e6 << " Msps" << std::endl;

        rtlsdr_set_tuner_gain_mode(m_dev.get(), 1);
        rtlsdr_set_tuner_gain(m_dev.get(), settings.tuner_gain);
        std::cout << "SDR: Tuner gain: " << settings.tuner_gain / 10.0 << " dB" << std::endl;

        rtlsdr_reset_buffer(m_dev.get());

        m_ref_latitude = settings.ref_latitude;
        m_ref_longitude = settings.ref_longitude;
        m_center_frequency = settings.center_frequency;
        m_sample_rate = settings.sample_rate;
        m_tuner_gain = settings.tuner_gain;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Initialization error: " << e.what() << std::endl;
        return false;
    }
}

std::pair<double, double> RTLSDRDevice::getRefPosition() const {
    return {m_ref_latitude, m_ref_longitude};
}

void RTLSDRDevice::startReceiving() {
    if (m_isReceiving) return;

    if (!m_queue) {
        std::cerr << "Error: Data queue not set. Call setQueue() first." << std::endl;
        return;
    }
    m_isReceiving = true;
    m_thread = std::thread(&RTLSDRDevice::receive, this);
}

void RTLSDRDevice::stopReceiving() {
    if (!m_isReceiving) return;
    m_isReceiving = false;

    if (m_queue) {
        m_queue->stop();
    }

    if (m_dev) {
        rtlsdr_cancel_async(m_dev.get());
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void RTLSDRDevice::receive() {
    std::cout << "SDR: Receive thread started." << std::endl;
    rtlsdr_read_async(m_dev.get(), RTLSDRDevice::callback, this, 16, 16 * 16384);
    std::cout << "SDR: Receive thread stopped." << std::endl;
}

}
