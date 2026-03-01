#include "MQTTPublisher.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>

#include <mqtt/client.h>
#include <mqtt/connect_options.h>

using namespace std::chrono_literals;

namespace jsignal {

MQTTPublisher::MQTTPublisher(const MQTTSettings& settings)
    : m_client(std::make_unique<mqtt::client>(settings.broker_address, settings.client_id)), m_topic_base(settings.topic_base) {}

MQTTPublisher::~MQTTPublisher() {
    try {
        if (m_client->is_connected()) {
            m_client->disconnect();
        }
    } catch (const mqtt::exception& exc) {
        std::cerr << "MQTT: Error trying to disconnect: " << exc.what() << std::endl;
    }
}

bool MQTTPublisher::connect() {
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);
    connOpts.set_automatic_reconnect(false);

    try {
        std::cout << "\033[1;36mMQTT: Trying to connect to:  "
                  << m_client->get_server_uri() << "\033[0m" << std::endl;

        m_client->connect(connOpts);

        if (m_client->is_connected()) {
            std::cout << "\033[1;36mMQTT: Connected.\033[0m" << std::endl;
            return true;
        }

    } catch (const mqtt::exception& exc) {
        std::cerr << "\033[1;31mMQTT: Connection error: " << exc.what()
                  << " (Code: " << exc.get_reason_code() << " - Type: " << exc.get_error_str() << ")\033[0m" << std::endl;
    }
    return false;
}

void MQTTPublisher::publish(const std::string& icao, const std::string& payload) {
    if (!m_client) return;

    std::string topic = m_topic_base + icao;
    const int max_retries = 2;

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        try {
            if (!m_client->is_connected()) {

                if (attempt == 0) {
                     std::cerr << "\033[1;33mMQTT: Connection lost. Trying again..\033[0m" << std::endl;

                     mqtt::connect_options connOpts;
                     connOpts.set_clean_session(true);

                     try {
                        m_client->connect(connOpts);
                        if (m_client->is_connected()) {
                            std::cout << "\033[1;32mMQTT: Connected.\033[0m" << std::endl;
                        } else {
                            throw mqtt::exception(mqtt::DISCONNECT_WITH_WILL_MESSAGE);
                        }
                     } catch (const mqtt::exception& conn_exc) {
                        throw mqtt::exception(mqtt::DISCONNECT_WITH_WILL_MESSAGE);
                     }
                }
            }

            m_client->publish(topic, payload.c_str(), payload.length(), 1, false);

            std::cout << "\033[0;34mMQTT: Published on " << topic << "\033[0m" << std::endl;
            return;

        } catch (const mqtt::exception& exc) {
            std::string error_msg = exc.what();

            if (error_msg.find("Disconnected") != std::string::npos && attempt == 0) {

                std::cerr << "\033[1;33mMQTT: Failed to reconnect.\033[0m" << std::endl;
                std::this_thread::sleep_for(100ms);
                continue;
            } else {
                std::cerr << "\033[1;31mMQTT: Error while publishing, message lost: " << error_msg << "\033[0m" << std::endl;
                return;
            }
        }
    }
}

}
