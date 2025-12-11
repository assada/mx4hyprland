#pragma once

#include "mx_master_4.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace mx4hyprland {

class HapticManager {
public:
    explicit HapticManager(std::unique_ptr<MXMaster4> device);
    ~HapticManager();

    HapticManager(const HapticManager&) = delete;
    HapticManager& operator=(const HapticManager&) = delete;
    HapticManager(HapticManager&&) = delete;
    HapticManager& operator=(HapticManager&&) = delete;

    void start();
    void stop();

    void trigger(int effect_id);

private:
    void worker_loop(std::stop_token stop_token);
    void safe_send(int effect_id);

    std::unique_ptr<MXMaster4> device_;
    std::queue<int> queue_;
    std::mutex queue_mutex_;
    std::condition_variable_any queue_cv_;

    std::jthread worker_thread_;
    std::atomic<bool> running_{false};

    static constexpr size_t MAX_QUEUE_SIZE = 10;
};

}
