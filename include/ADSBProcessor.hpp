#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <optional>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "adsb/types.hpp"
#include "adsb/message/ADSBMessage.hpp"
#include "adsb/message/IdentificationMessage.hpp"
#include "adsb/message/AirbornePositionMessage.hpp"
#include "adsb/message/VelocityMessage.hpp"
#include "MQTTPublisher.hpp"

namespace jsignal {

    class AircraftState {
    public:
        AircraftState() : m_icao(""), m_last_seen_ms(0) {}
        explicit AircraftState(std::string icao) : m_icao(std::move(icao)), m_last_seen_ms(0) {}

        void update_with_message(std::unique_ptr<adsb::message::ADSBMessage> msg);

        std::optional<adsb::types::PositionResult> calculate_position(const adsb::types::GlobalPosition& ref_pos);

        const std::string& get_icao() const { return m_icao; }
        long long get_last_seen_ms() const { return m_last_seen_ms; }

        const std::unique_ptr<adsb::message::IdentificationMessage>& get_last_identification() const {
            return m_last_identification;
        }
        const std::unique_ptr<adsb::message::VelocityMessage>& get_last_velocity() const {
            return m_last_velocity;
        }

        const std::unique_ptr<adsb::message::AirbornePositionMessage>& get_last_position() const {
            return (m_last_pos_odd) ? m_last_pos_odd : m_last_pos_even;
        }

    private:
        bool is_timestamp_valid(const adsb::message::AirbornePositionMessage& msg1,
                                const adsb::message::AirbornePositionMessage& msg2) const;

        std::string m_icao;
        long long m_last_seen_ms;

        std::unique_ptr<adsb::message::IdentificationMessage> m_last_identification;
        std::unique_ptr<adsb::message::VelocityMessage> m_last_velocity;
        std::unique_ptr<adsb::message::AirbornePositionMessage> m_last_pos_odd;
        std::unique_ptr<adsb::message::AirbornePositionMessage> m_last_pos_even;
    };


    class ADSBProcessor {
    public:
        ADSBProcessor(adsb::types::GlobalPosition ref_pos, MQTTPublisher& publisher);
        ~ADSBProcessor();

        void processRawData(const uint8_t* buf, uint32_t len);

    private:

        void detect_messages();
        void demodulate_message(size_t offset);

        std::vector<double> m_magnitude_buffer;
        size_t m_scan_offset = 0;

        std::string format_aircraft_data(const AircraftState& aircraft,
                                         const std::optional<adsb::types::PositionResult>& pos_result) const;
        json format_message_data(const adsb::message::ADSBMessage& msg) const;
        std::string format_double(double val, int precision) const;

        adsb::types::GlobalPosition m_ref_pos;
        MQTTPublisher& m_publisher;
        std::map<std::string, AircraftState> m_aircraft_db;
        std::mutex m_db_mutex;
    };

}

