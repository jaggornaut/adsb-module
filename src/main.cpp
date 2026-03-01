#include "SDRDevice.hpp"
#include "RTLSDRDevice.hpp"
#include "ADSBProcessor.hpp"
#include "MQTTPublisher.hpp"
#include "DataQueue.hpp"
#include "SDRSettings.hpp"
#include "adsb/types.hpp"

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <utility>
#include <csignal>
#include <condition_variable>
#include <yaml-cpp/yaml.h>
#include <filesystem>

static std::condition_variable g_shutdown_cv;
static std::mutex g_shutdown_mutex;
static std::atomic<bool> g_shutdown_requested(false);

void signal_handler(int) {
    g_shutdown_requested = true;
    g_shutdown_cv.notify_all();
}

struct AppSettings {
    jsignal::SDRSettings sdr_settings;
    jsignal::MQTTSettings mqtt_settings;
};

AppSettings loadConfig(const std::string& configPath) {
    if (!std::filesystem::exists(configPath)) {
        throw std::runtime_error("Configuration file not found: " + configPath);
    }
    YAML::Node config = YAML::LoadFile(configPath);
    AppSettings settings;
    const auto& sdr_node = config["sdr_settings"];
    settings.sdr_settings.device_index = sdr_node["device_index"].as<int>();
    settings.sdr_settings.center_frequency = sdr_node["center_frequency"].as<uint32_t>();
    settings.sdr_settings.sample_rate = sdr_node["sample_rate"].as<uint32_t>();
    settings.sdr_settings.tuner_gain = sdr_node["tuner_gain"].as<int>();
    settings.sdr_settings.ref_latitude = sdr_node["ref_position"]["latitude"].as<double>();
    settings.sdr_settings.ref_longitude = sdr_node["ref_position"]["longitude"].as<double>();
    const auto& mqtt_node = config["mqtt_settings"];
    settings.mqtt_settings.broker_address = mqtt_node["broker_address"].as<std::string>();
    settings.mqtt_settings.client_id = mqtt_node["client_id"].as<std::string>();
    settings.mqtt_settings.topic_base = mqtt_node["topic_base"].as<std::string>();
    return settings;
}

void processing_loop(jsignal::ADSBProcessor& processor,
                     jsignal::DataQueue& queue,
                     std::atomic<bool>& running)
{
    std::cout << "CPU: Processing thread started." << std::endl;
    while (running) {
        jsignal::DataBuffer data = queue.pop();

        if (data.empty()) {
            continue;
        }

        processor.processRawData(data.data(), data.size());
    }
    std::cout << "CPU: Processing thread stopped." << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_config.yaml>" << std::endl;
        return 1;
    }
    AppSettings appSettings = loadConfig(argv[1]);

    try {
        auto sdr_driver = std::make_unique<jsignal::RTLSDRDevice>();
        if (!sdr_driver->init(appSettings.sdr_settings)) {
            return 1;
        }

        const auto [lat, lon] = sdr_driver->getRefPosition();
        adsb::types::GlobalPosition ref_pos = {lat, lon};

        jsignal::MQTTPublisher mqtt_publisher(appSettings.mqtt_settings);
        {
            constexpr int max_retries = 10;
            constexpr int retry_delay_s = 3;
            bool connected = false;
            for (int i = 1; i <= max_retries && !g_shutdown_requested; ++i) {
                if (mqtt_publisher.connect()) {
                    connected = true;
                    break;
                }
                std::cerr << "MQTT: Retry " << i << "/" << max_retries
                          << " in " << retry_delay_s << "s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s));
            }
            if (!connected) {
                std::cerr << "Error: Could not connect to MQTT broker after "
                          << max_retries << " attempts. Aborting." << std::endl;
                return 1;
            }
        }

        jsignal::ADSBProcessor adsb_logic(ref_pos, mqtt_publisher);

        jsignal::DataQueue data_queue;
        std::atomic<bool> running(true);
        std::thread processing_thread(processing_loop,
                                      std::ref(adsb_logic),
                                      std::ref(data_queue),
                                      std::ref(running));

        sdr_driver->setQueue(&data_queue);

        sdr_driver->startReceiving();

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::cout << "\n>>> Running. Send SIGINT/SIGTERM to stop. <<<\n" << std::endl;
        {
            std::unique_lock<std::mutex> lock(g_shutdown_mutex);
            g_shutdown_cv.wait(lock, [] { return g_shutdown_requested.load(); });
        }

        std::cout << "\n>>> Shutting down... <<<\n" << std::endl;

        running = false;
        sdr_driver->stopReceiving();

        if (processing_thread.joinable()) {
            processing_thread.join();
        }

        std::cout << ">>> Program terminated. <<<\n" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception in main: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
