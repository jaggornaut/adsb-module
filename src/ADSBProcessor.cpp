#include "ADSBProcessor.hpp"
#include "adsb/decoder.hpp"
#include "adsb/message/IdentificationMessage.hpp"
#include "adsb/message/AirbornePositionMessage.hpp"
#include "adsb/message/VelocityMessage.hpp"
#include "adsb/types.hpp"
#include "MQTTPublisher.hpp"

#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <mutex>

namespace jsignal {

static long long get_current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void AircraftState::update_with_message(std::unique_ptr<adsb::message::ADSBMessage> msg) {
    if (!msg) return;
    if (m_icao.empty()) m_icao = msg->get_icao();
    m_last_seen_ms = get_current_time_ms();

    if (auto* id_msg = dynamic_cast<adsb::message::IdentificationMessage*>(msg.get())) {
        m_last_identification.reset(
            static_cast<adsb::message::IdentificationMessage*>(msg.release()));
    } else if (auto* vel_msg = dynamic_cast<adsb::message::VelocityMessage*>(msg.get())) {
        m_last_velocity.reset(
            static_cast<adsb::message::VelocityMessage*>(msg.release()));
    } else if (auto* pos_msg = dynamic_cast<adsb::message::AirbornePositionMessage*>(msg.get())) {
        if (pos_msg->is_odd_frame()) {
            m_last_pos_odd.reset(
                static_cast<adsb::message::AirbornePositionMessage*>(msg.release()));
        } else {
            m_last_pos_even.reset(
                static_cast<adsb::message::AirbornePositionMessage*>(msg.release()));
        }
    }
}

bool AircraftState::is_timestamp_valid(const adsb::message::AirbornePositionMessage& msg1,
                                       const adsb::message::AirbornePositionMessage& msg2) const {
    const auto MAX_TIME_DIFF = std::chrono::seconds(10);
    auto diff = msg1.get_timestamp() - msg2.get_timestamp();
    return std::abs(std::chrono::duration_cast<std::chrono::seconds>(diff).count())
           < MAX_TIME_DIFF.count();
}

std::optional<adsb::types::PositionResult>
AircraftState::calculate_position(const adsb::types::GlobalPosition& ref_pos) {
    if (!m_last_pos_odd || !m_last_pos_even) return std::nullopt;


    if (!is_timestamp_valid(*m_last_pos_odd, *m_last_pos_even)) {
        auto t_odd  = m_last_pos_odd->get_timestamp();
        auto t_even = m_last_pos_even->get_timestamp();
        if (t_odd > t_even) m_last_pos_even.reset();
        else                m_last_pos_odd.reset();
        return std::nullopt;
    }

    try {
        auto result = adsb::decoder::calculate_global_position(
            *m_last_pos_odd, *m_last_pos_even, ref_pos);

        if (!result.is_valid) return std::nullopt;

        m_last_pos_odd.reset();
        m_last_pos_even.reset();

        return result;
    } catch (...) {
        return std::nullopt;
    }
}

static constexpr size_t PREAMBLE_SAMPLES  = 16;
static constexpr size_t MSG_BITS          = 112;
static constexpr size_t MSG_SAMPLES       = MSG_BITS * 2;           // 224
static constexpr size_t TOTAL_SAMPLES     = PREAMBLE_SAMPLES + MSG_SAMPLES; // 240

ADSBProcessor::ADSBProcessor(adsb::types::GlobalPosition ref_pos, MQTTPublisher& publisher)
    : m_ref_pos(ref_pos), m_publisher(publisher)
{
    std::cout << "ADSBProcessor: Initialized" << std::endl;
}

ADSBProcessor::~ADSBProcessor() = default;

void ADSBProcessor::processRawData(const uint8_t* buf, uint32_t len) {
    if (!buf || len < 2) return;

    const uint32_t num_samples = len / 2;
    for (uint32_t i = 0; i < num_samples; ++i) {
        double I = static_cast<double>(buf[i * 2])     - 127.5;
        double Q = static_cast<double>(buf[i * 2 + 1]) - 127.5;
        m_magnitude_buffer.push_back(I * I + Q * Q);
    }

    detect_messages();

    static constexpr size_t COMPACTION_THRESHOLD = TOTAL_SAMPLES * 10;
    if (m_scan_offset > COMPACTION_THRESHOLD) {
        m_magnitude_buffer.erase(m_magnitude_buffer.begin(),
                                 m_magnitude_buffer.begin() + m_scan_offset);
        m_scan_offset = 0;
    }
}

// ---------------------------------------------------------------------------
//   Mode S preamble at 2 Msps:
//   sample:    0  1  2  3  4  5  6  7  8  9 ...
//   expected:  H  L  H  L  L  L  L  H  L  H ...
//   peaks: 0, 2, 7, 9  —  valleys: 1, 3, 4, 5, 6, 8
// ---------------------------------------------------------------------------
void ADSBProcessor::detect_messages() {
    if (m_magnitude_buffer.size() < m_scan_offset + TOTAL_SAMPLES) return;

    const double* m = m_magnitude_buffer.data();
    const size_t  limit = m_magnitude_buffer.size() - TOTAL_SAMPLES;

    for (size_t i = m_scan_offset; i < limit; ++i) {
        // Check all 4 peaks
        if (m[i]     <= m[i + 1]) continue; // peak 0 - valley 1
        if (m[i + 2] <= m[i + 3]) continue; // peak 2 - valley 3
        if (m[i + 7] <= m[i + 6]) continue; // peak 7 - valley 6
        if (m[i + 9] <= m[i + 8]) continue; // peak 9 - valley 8

        double mean_peaks = (m[i] + m[i+2] + m[i+7] + m[i+9]) / 4.0;
        if (m[i + 4] > mean_peaks / 2.0) continue;
        if (m[i + 5] > mean_peaks / 2.0) continue;
        if (m[i + 6] > mean_peaks / 2.0) continue;

        demodulate_message(i + PREAMBLE_SAMPLES);

        i += TOTAL_SAMPLES - 1;
    }

    m_scan_offset = limit;
}

void ADSBProcessor::demodulate_message(size_t offset) {
    if (offset + MSG_SAMPLES > m_magnitude_buffer.size()) return;

    const double* m = m_magnitude_buffer.data();

    std::vector<int> bits(MSG_BITS);
    for (size_t j = 0; j < MSG_BITS; ++j) {
        size_t s = offset + j * 2;
        bits[j] = (m[s] > m[s + 1]) ? 1 : 0;
    }

    auto decoded = adsb::decoder::decode(bits);
    if (!decoded) return;

    std::lock_guard<std::mutex> lock(m_db_mutex);

    const std::string icao = decoded->get_icao();
    auto& aircraft = m_aircraft_db[icao];
    aircraft.update_with_message(std::move(decoded));

    auto pos_result = aircraft.calculate_position(m_ref_pos);
    std::string payload = format_aircraft_data(aircraft, pos_result);
    m_publisher.publish(icao, payload);
}


std::string ADSBProcessor::format_double(double val, int precision) const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << val;
    return ss.str();
}

std::string ADSBProcessor::format_aircraft_data(
    const AircraftState& aircraft,
    const std::optional<adsb::types::PositionResult>& pos_result) const
{
    json data;
    data["icao"]         = aircraft.get_icao();
    data["last_seen_ms"] = aircraft.get_last_seen_ms();

    if (const auto& id = aircraft.get_last_identification()) {
        data["callsign"]              = id->get_flight_name();
        data["emitter_category_name"] = to_string(id->get_category());
    }
    if (const auto& vel = aircraft.get_last_velocity()) {
        data["ground_speed_kts"]  = format_double(vel->get_speed(), 2);
        data["heading_deg"]       = format_double(vel->get_heading(), 2);
        data["vertical_rate_fpm"] = vel->get_vertical_rate();
    }
    if (const auto& pos = aircraft.get_last_position()) {
        data["altitude_barometric_ft"] = pos->get_altitude();
    }
    if (pos_result) {
        data["latitude"]     = format_double(pos_result->position.latitude, 5);
        data["longitude"]    = format_double(pos_result->position.longitude, 5);
        data["altitude_gps"] = pos_result->position.altitude;
    }
    return data.dump();
}

json ADSBProcessor::format_message_data(const adsb::message::ADSBMessage& msg) const {
    json j;
    j["icao"]            = msg.get_icao();
    j["message_type_id"] = msg.get_type_code();

    if (const auto* p = dynamic_cast<const adsb::message::AirbornePositionMessage*>(&msg)) {
        j["message_type_name"]   = "AirbornePosition";
        j["altitude_barometric"] = p->get_altitude();
    } else if (const auto* id = dynamic_cast<const adsb::message::IdentificationMessage*>(&msg)) {
        j["message_type_name"]     = "Identification";
        j["callsign"]              = id->get_flight_name();
        j["emitter_category_code"] = id->get_category();
        j["emitter_category_name"] = to_string(id->get_category());
    } else if (const auto* vel = dynamic_cast<const adsb::message::VelocityMessage*>(&msg)) {
        j["message_type_name"]     = "Velocity";
        j["ground_speed_kts"]      = format_double(vel->get_speed(), 2);
        j["heading_deg"]           = format_double(vel->get_heading(), 2);
        j["vertical_rate_fpm"]     = vel->get_vertical_rate();
    }
    return j;
}

}