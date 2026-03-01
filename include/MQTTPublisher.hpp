#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mqtt/client.h>

namespace jsignal {

struct MQTTSettings {
    std::string broker_address;
    std::string client_id;
    std::string topic_base;
};

class MQTTPublisher {
public:
    MQTTPublisher(const MQTTSettings& settings);

    ~MQTTPublisher();

    bool connect();

    void publish(const std::string& icao, const std::string& payload);

private:
    std::string m_topic_base;

    std::unique_ptr<mqtt::client> m_client;

    std::unique_ptr<std::thread> m_conn_thread;
    std::atomic<bool> m_stop_thread{false};

    void connection_loop();
};

}
