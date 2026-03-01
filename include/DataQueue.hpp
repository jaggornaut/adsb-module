#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace jsignal {

    using DataBuffer = std::vector<uint8_t>;

    class DataQueue {
    public:
        DataQueue() : m_running(true) {}

        void push(DataBuffer&& data) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(std::move(data));
            }
            m_cond.notify_one();
        }

        DataBuffer pop() {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cond.wait(lock, [this] {
                return !m_queue.empty() || !m_running;
            });

            if (!m_running && m_queue.empty()) {
                return {};
            }

            DataBuffer data = std::move(m_queue.front());
            m_queue.pop();
            return data;
        }

        void stop() {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_running = false;
            }
            m_cond.notify_all();
        }

    private:
        bool m_running;
        std::queue<DataBuffer> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cond;
    };

}