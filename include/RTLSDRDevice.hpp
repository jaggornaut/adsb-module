#include "SDRDevice.hpp"
#include "DataQueue.hpp"
#include <rtl-sdr.h>
#include <memory>
#include <thread>
#include <atomic>

namespace jsignal {

class RTLSDRDevice : public SDRDevice {
public:
    RTLSDRDevice();
    ~RTLSDRDevice() override;

    bool init(const SDRSettings& settings) override;
    std::pair<double, double> getRefPosition() const override;

    void startReceiving() override;
    void stopReceiving() override;

    void setQueue(DataQueue* queue) {
        m_queue = queue;
    }

private:
    void receive();
    static void callback(uint8_t* buf, uint32_t len, void* ctx);

    std::unique_ptr<rtlsdr_dev_t, decltype(&rtlsdr_close)> m_dev;
    std::thread   m_thread;
    std::atomic<bool> m_isReceiving;

    DataQueue* m_queue;

    double   m_ref_latitude;
    double   m_ref_longitude;
    uint32_t m_center_frequency;
    uint32_t m_sample_rate;
    int      m_tuner_gain;
};

}

